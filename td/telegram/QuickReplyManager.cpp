//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/QuickReplyManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/Dependencies.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/logevent/LogEventHelper.h"
#include "td/telegram/MessageContent.h"
#include "td/telegram/MessageContentType.h"
#include "td/telegram/MessageReplyHeader.h"
#include "td/telegram/MessageSelfDestructType.h"
#include "td/telegram/misc.h"
#include "td/telegram/RepliedMessageInfo.h"
#include "td/telegram/Td.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"

namespace td {

class GetQuickRepliesQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::messages_QuickReplies>> promise_;

 public:
  explicit GetQuickRepliesQuery(Promise<telegram_api::object_ptr<telegram_api::messages_QuickReplies>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(int64 hash) {
    send_query(G()->net_query_creator().create(telegram_api::messages_getQuickReplies(hash), {{"me"}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getQuickReplies>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(DEBUG) << "Receive result for GetQuickRepliesQuery: " << to_string(ptr);
    promise_.set_value(std::move(ptr));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class DeleteQuickReplyShortcutQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit DeleteQuickReplyShortcutQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(QuickReplyShortcutId shortcut_id) {
    send_query(
        G()->net_query_creator().create(telegram_api::messages_deleteQuickReplyShortcut(shortcut_id.get()), {{"me"}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_deleteQuickReplyShortcut>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    td_->quick_reply_manager_->reload_quick_reply_shortcuts();
    promise_.set_error(std::move(status));
  }
};

class ReorderQuickRepliesQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ReorderQuickRepliesQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(vector<QuickReplyShortcutId> shortcut_ids) {
    send_query(
        G()->net_query_creator().create(telegram_api::messages_reorderQuickReplies(
                                            QuickReplyShortcutId::get_input_quick_reply_shortcut_ids(shortcut_ids)),
                                        {{"me"}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_reorderQuickReplies>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    td_->quick_reply_manager_->reload_quick_reply_shortcuts();
    promise_.set_error(std::move(status));
  }
};

QuickReplyManager::QuickReplyMessage::~QuickReplyMessage() = default;

template <class StorerT>
void QuickReplyManager::QuickReplyMessage::store(StorerT &storer) const {
  bool is_server = message_id.is_server();
  bool has_edit_date = edit_date != 0;
  bool has_random_id = !is_server && random_id != 0;
  bool has_reply_to_message_id = reply_to_message_id != MessageId();
  bool has_send_emoji = !is_server && !send_emoji.empty();
  bool has_via_bot_user_id = via_bot_user_id != UserId();
  bool has_legacy_layer = legacy_layer != 0;
  bool has_send_error_code = !is_server && send_error_code != 0;
  bool has_send_error_message = !is_server && !send_error_message.empty();
  bool has_try_resend_at = !is_server && try_resend_at != 0;
  bool has_media_album_id = media_album_id != 0;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_edit_date);
  STORE_FLAG(has_random_id);
  STORE_FLAG(has_reply_to_message_id);
  STORE_FLAG(has_send_emoji);
  STORE_FLAG(has_via_bot_user_id);
  STORE_FLAG(is_failed_to_send);
  STORE_FLAG(disable_notification);
  STORE_FLAG(noforwards);
  STORE_FLAG(invert_media);
  STORE_FLAG(from_background);
  STORE_FLAG(disable_web_page_preview);
  STORE_FLAG(hide_via_bot);
  STORE_FLAG(has_legacy_layer);
  STORE_FLAG(has_send_error_code);
  STORE_FLAG(has_send_error_message);
  STORE_FLAG(has_try_resend_at);
  STORE_FLAG(has_media_album_id);
  END_STORE_FLAGS();
  td::store(message_id, storer);
  td::store(shortcut_id, storer);
  if (has_edit_date) {
    td::store(edit_date, storer);
  }
  if (has_random_id) {
    td::store(random_id, storer);
  }
  if (has_reply_to_message_id) {
    td::store(reply_to_message_id, storer);
  }
  if (has_send_emoji) {
    td::store(send_emoji, storer);
  }
  if (has_via_bot_user_id) {
    td::store(via_bot_user_id, storer);
  }
  if (has_legacy_layer) {
    td::store(legacy_layer, storer);
  }
  if (has_send_error_code) {
    td::store(send_error_code, storer);
  }
  if (has_send_error_message) {
    td::store(send_error_message, storer);
  }
  if (has_try_resend_at) {
    td::store_time(try_resend_at, storer);
  }
  if (has_media_album_id) {
    td::store(media_album_id, storer);
  }
  store_message_content(content.get(), storer);
}

template <class ParserT>
void QuickReplyManager::QuickReplyMessage::parse(ParserT &parser) {
  bool has_edit_date;
  bool has_random_id;
  bool has_reply_to_message_id;
  bool has_send_emoji;
  bool has_via_bot_user_id;
  bool has_legacy_layer;
  bool has_send_error_code;
  bool has_send_error_message;
  bool has_try_resend_at;
  bool has_media_album_id;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_edit_date);
  PARSE_FLAG(has_random_id);
  PARSE_FLAG(has_reply_to_message_id);
  PARSE_FLAG(has_send_emoji);
  PARSE_FLAG(has_via_bot_user_id);
  PARSE_FLAG(is_failed_to_send);
  PARSE_FLAG(disable_notification);
  PARSE_FLAG(noforwards);
  PARSE_FLAG(invert_media);
  PARSE_FLAG(from_background);
  PARSE_FLAG(disable_web_page_preview);
  PARSE_FLAG(hide_via_bot);
  PARSE_FLAG(has_legacy_layer);
  PARSE_FLAG(has_send_error_code);
  PARSE_FLAG(has_send_error_message);
  PARSE_FLAG(has_try_resend_at);
  PARSE_FLAG(has_media_album_id);
  END_PARSE_FLAGS();
  td::parse(message_id, parser);
  td::parse(shortcut_id, parser);
  if (has_edit_date) {
    td::parse(edit_date, parser);
  }
  if (has_random_id) {
    td::parse(random_id, parser);
  }
  if (has_reply_to_message_id) {
    td::parse(reply_to_message_id, parser);
  }
  if (has_send_emoji) {
    td::parse(send_emoji, parser);
  }
  if (has_via_bot_user_id) {
    td::parse(via_bot_user_id, parser);
  }
  if (has_legacy_layer) {
    td::parse(legacy_layer, parser);
  }
  if (has_send_error_code) {
    td::parse(send_error_code, parser);
  }
  if (has_send_error_message) {
    td::parse(send_error_message, parser);
  }
  if (has_try_resend_at) {
    td::parse_time(try_resend_at, parser);
  }
  if (has_media_album_id) {
    td::parse(media_album_id, parser);
  }
  parse_message_content(content, parser);
}

QuickReplyManager::Shortcut::~Shortcut() = default;

template <class StorerT>
void QuickReplyManager::Shortcut::store(StorerT &storer) const {
  bool has_server_total_count = server_total_count_ != 0;
  bool has_local_total_count = local_total_count_ != 0;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_server_total_count);
  STORE_FLAG(has_local_total_count);
  END_STORE_FLAGS();
  td::store(name_, storer);
  td::store(shortcut_id_, storer);
  if (has_server_total_count) {
    td::store(server_total_count_, storer);
  }
  if (has_local_total_count) {
    td::store(local_total_count_, storer);
  }
  td::store(messages_, storer);
}

template <class ParserT>
void QuickReplyManager::Shortcut::parse(ParserT &parser) {
  bool has_server_total_count;
  bool has_local_total_count;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_server_total_count);
  PARSE_FLAG(has_local_total_count);
  END_PARSE_FLAGS();
  td::parse(name_, parser);
  td::parse(shortcut_id_, parser);
  if (has_server_total_count) {
    td::parse(server_total_count_, parser);
  }
  if (has_local_total_count) {
    td::parse(local_total_count_, parser);
  }
  td::parse(messages_, parser);
}

template <class StorerT>
void QuickReplyManager::Shortcuts::store(StorerT &storer) const {
  BEGIN_STORE_FLAGS();
  END_STORE_FLAGS();
  td::store(shortcuts_, storer);
}

template <class ParserT>
void QuickReplyManager::Shortcuts::parse(ParserT &parser) {
  BEGIN_PARSE_FLAGS();
  END_PARSE_FLAGS();
  td::parse(shortcuts_, parser);
}

QuickReplyManager::QuickReplyManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void QuickReplyManager::tear_down() {
  parent_.reset();
}

unique_ptr<QuickReplyManager::QuickReplyMessage> QuickReplyManager::create_message(
    telegram_api::object_ptr<telegram_api::Message> message_ptr, const char *source) const {
  LOG(DEBUG) << "Receive from " << source << " " << to_string(message_ptr);
  CHECK(message_ptr != nullptr);

  switch (message_ptr->get_id()) {
    case telegram_api::messageEmpty::ID:
      break;
    case telegram_api::message::ID: {
      auto message_id = MessageId::get_message_id(message_ptr, false);
      auto message = move_tl_object_as<telegram_api::message>(message_ptr);
      if (message->quick_reply_shortcut_id_ == 0) {
        LOG(ERROR) << "Receive a quick reply without shortcut from " << source;
        break;
      }

      auto my_dialog_id = td_->dialog_manager_->get_my_dialog_id();
      if (DialogId(message->peer_id_) != my_dialog_id || message->from_id_ != nullptr ||
          message->saved_peer_id_ != nullptr || message->fwd_from_ != nullptr || message->views_ != 0 ||
          message->forwards_ != 0 || message->replies_ != nullptr || message->reactions_ != nullptr ||
          message->ttl_period_ != 0 || !message->out_ || message->post_ || message->edit_hide_ ||
          message->from_scheduled_ || message->pinned_ || message->noforwards_ || message->mentioned_ ||
          message->media_unread_ || message->reply_markup_ != nullptr || !message->restriction_reason_.empty() ||
          !message->post_author_.empty() || message->from_boosts_applied_ != 0) {
        LOG(ERROR) << "Receive an invalid quick reply from " << source << ": " << to_string(message);
      }

      UserId via_bot_user_id;
      if (message->flags_ & telegram_api::message::VIA_BOT_ID_MASK) {
        via_bot_user_id = UserId(message->via_bot_id_);
        if (!via_bot_user_id.is_valid()) {
          LOG(ERROR) << "Receive invalid " << via_bot_user_id << " from " << source;
          via_bot_user_id = UserId();
        }
      }
      auto media_album_id = message->grouped_id_;

      MessageSelfDestructType ttl;
      bool disable_web_page_preview = false;
      auto content = get_message_content(
          td_,
          get_message_text(td_->contacts_manager_.get(), std::move(message->message_), std::move(message->entities_),
                           true, td_->auth_manager_->is_bot(), 0, media_album_id != 0, source),
          std::move(message->media_), my_dialog_id, message->date_, true, via_bot_user_id, &ttl,
          &disable_web_page_preview, source);

      auto reply_header = MessageReplyHeader(td_, std::move(message->reply_to_), my_dialog_id, message_id, -1, false);
      if (reply_header.story_full_id_ != StoryFullId()) {
        LOG(ERROR) << "Receive reply to " << reply_header.story_full_id_;
        reply_header.story_full_id_ = {};
      }
      if (reply_header.replied_message_info_.is_external() ||
          reply_header.replied_message_info_.get_reply_message_full_id(DialogId(), true).get_dialog_id() !=
              DialogId()) {
        LOG(ERROR) << "Receive reply to " << reply_header.replied_message_info_;
        reply_header.replied_message_info_ = {};
      }
      auto reply_to_message_id = reply_header.replied_message_info_.get_same_chat_reply_to_message_id(true);

      if (!ttl.is_empty()) {
        LOG(ERROR) << "Wrong " << ttl << " received in " << message_id << " from " << source;
        break;
      }

      auto content_type = content->get_type();
      if (is_expired_message_content(content_type)) {
        LOG(ERROR) << "Receive " << content_type << " from " << source;
        break;
      }

      auto result = make_unique<QuickReplyMessage>();
      result->shortcut_id = QuickReplyShortcutId(message->quick_reply_shortcut_id_);
      result->message_id = message_id;
      result->edit_date = max(message->edit_date_, 0);
      result->disable_web_page_preview = disable_web_page_preview;
      result->reply_to_message_id = reply_to_message_id;
      result->via_bot_user_id = via_bot_user_id;
      result->disable_notification = message->silent_;
      result->legacy_layer = (message->legacy_ ? MTPROTO_LAYER : 0);
      result->invert_media = message->invert_media_;
      result->content = std::move(content);

      if (media_album_id != 0) {
        if (!is_allowed_media_group_content(content_type)) {
          if (content_type != MessageContentType::Unsupported) {
            LOG(ERROR) << "Receive media group identifier " << media_album_id << " in " << message_id
                       << " with content "
                       << oneline(to_string(get_quick_reply_message_message_content_object(result.get())));
          }
        } else {
          result->media_album_id = media_album_id;
        }
      }

      Dependencies dependencies;
      add_quick_reply_message_dependencies(dependencies, result.get());
      for (auto dependent_dialog_id : dependencies.get_dialog_ids()) {
        td_->dialog_manager_->force_create_dialog(dependent_dialog_id, source, true);
      }

      return result;
    }
    case telegram_api::messageService::ID:
      LOG(ERROR) << "Receive " << to_string(message_ptr);
      break;
    default:
      UNREACHABLE();
      break;
  }
  return nullptr;
}

void QuickReplyManager::add_quick_reply_message_dependencies(Dependencies &dependencies,
                                                             const QuickReplyMessage *m) const {
  auto is_bot = td_->auth_manager_->is_bot();
  dependencies.add(m->via_bot_user_id);
  add_message_content_dependencies(dependencies, m->content.get(), is_bot);
}

bool QuickReplyManager::can_edit_quick_reply_message(const QuickReplyMessage *m) const {
  return m->message_id.is_server() && !m->via_bot_user_id.is_valid() &&
         is_editable_message_content(m->content->get_type());
}

bool QuickReplyManager::can_resend_quick_reply_message(const QuickReplyMessage *m) const {
  if (m->send_error_code != 429) {
    return false;
  }
  if (m->via_bot_user_id.is_valid() || m->hide_via_bot) {
    return false;
  }
  return true;
}

td_api::object_ptr<td_api::MessageSendingState> QuickReplyManager::get_message_sending_state_object(
    const QuickReplyMessage *m) const {
  CHECK(m != nullptr);
  if (m->message_id.is_yet_unsent()) {
    return td_api::make_object<td_api::messageSendingStatePending>(m->sending_id);
  }
  if (m->is_failed_to_send) {
    auto can_retry = can_resend_quick_reply_message(m);
    auto error_code = m->send_error_code > 0 ? m->send_error_code : 400;
    auto need_another_reply_quote =
        can_retry && error_code == 400 && m->send_error_message == CSlice("QUOTE_TEXT_INVALID");
    return td_api::make_object<td_api::messageSendingStateFailed>(
        td_api::make_object<td_api::error>(error_code, m->send_error_message), can_retry, false,
        need_another_reply_quote, false, max(m->try_resend_at - Time::now(), 0.0));
  }
  return nullptr;
}

td_api::object_ptr<td_api::MessageContent> QuickReplyManager::get_quick_reply_message_message_content_object(
    const QuickReplyMessage *m) const {
  return get_message_content_object(m->content.get(), td_, DialogId(), 0, false, true, -1, m->invert_media,
                                    m->disable_web_page_preview);
}

td_api::object_ptr<td_api::quickReplyMessage> QuickReplyManager::get_quick_reply_message_object(
    const QuickReplyMessage *m, const char *source) const {
  CHECK(m != nullptr);
  auto can_be_edited = can_edit_quick_reply_message(m);
  return td_api::make_object<td_api::quickReplyMessage>(
      m->message_id.get(), get_message_sending_state_object(m), can_be_edited, m->reply_to_message_id.get(),
      td_->contacts_manager_->get_user_id_object(m->via_bot_user_id, "via_bot_user_id"), m->media_album_id,
      get_quick_reply_message_message_content_object(m));
}

int32 QuickReplyManager::get_shortcut_message_count(const Shortcut *s) {
  return max(s->server_total_count_ + s->local_total_count_, static_cast<int32>(s->messages_.size()));
}

td_api::object_ptr<td_api::quickReplyShortcut> QuickReplyManager::get_quick_reply_shortcut_object(
    const Shortcut *s, const char *source) const {
  CHECK(s != nullptr);
  CHECK(!s->messages_.empty());
  return td_api::make_object<td_api::quickReplyShortcut>(s->shortcut_id_.get(), s->name_,
                                                         get_quick_reply_message_object(s->messages_[0].get(), source),
                                                         get_shortcut_message_count(s));
}

void QuickReplyManager::get_quick_reply_shortcuts(Promise<Unit> &&promise) {
  if (shortcuts_.are_inited_) {
    return promise.set_value(Unit());
  }

  load_quick_reply_shortcuts(std::move(promise));
}

void QuickReplyManager::load_quick_reply_shortcuts(Promise<Unit> &&promise) {
  shortcuts_.load_queries_.push_back(std::move(promise));
  if (shortcuts_.load_queries_.size() != 1) {
    return;
  }
  reload_quick_reply_shortcuts();
}

void QuickReplyManager::reload_quick_reply_shortcuts() {
  auto promise = PromiseCreator::lambda(
      [actor_id = actor_id(this)](Result<telegram_api::object_ptr<telegram_api::messages_QuickReplies>> r_shortcuts) {
        send_closure(actor_id, &QuickReplyManager::on_reload_quick_reply_shortcuts, std::move(r_shortcuts));
      });
  td_->create_handler<GetQuickRepliesQuery>(std::move(promise))->send(get_shortcuts_hash());
}

void QuickReplyManager::on_reload_quick_reply_shortcuts(
    Result<telegram_api::object_ptr<telegram_api::messages_QuickReplies>> r_shortcuts) {
  G()->ignore_result_if_closing(r_shortcuts);
  if (r_shortcuts.is_error()) {
    return on_load_quick_reply_fail(r_shortcuts.move_as_error());
  }
  auto shortcuts_ptr = r_shortcuts.move_as_ok();
  switch (shortcuts_ptr->get_id()) {
    case telegram_api::messages_quickRepliesNotModified::ID:
      if (!shortcuts_.are_inited_) {
        shortcuts_.are_inited_ = true;
        save_quick_reply_shortcuts();
        send_update_quick_reply_shortcuts();
      }
      break;
    case telegram_api::messages_quickReplies::ID: {
      auto shortcuts = telegram_api::move_object_as<telegram_api::messages_quickReplies>(shortcuts_ptr);
      td_->contacts_manager_->on_get_users(std::move(shortcuts->users_), "messages.quickReplies");
      td_->contacts_manager_->on_get_chats(std::move(shortcuts->chats_), "messages.quickReplies");

      FlatHashMap<MessageId, telegram_api::object_ptr<telegram_api::Message>, MessageIdHash> message_id_to_message;
      for (auto &message : shortcuts->messages_) {
        auto message_id = MessageId::get_message_id(message, false);
        if (!message_id.is_valid()) {
          continue;
        }
        message_id_to_message[message_id] = std::move(message);
      }

      FlatHashSet<QuickReplyShortcutId, QuickReplyShortcutIdHash> old_shortcut_ids;
      for (auto &shortcut : shortcuts_.shortcuts_) {
        old_shortcut_ids.insert(shortcut->shortcut_id_);
      }
      FlatHashSet<QuickReplyShortcutId, QuickReplyShortcutIdHash> added_shortcut_ids;
      FlatHashSet<string> added_shortcut_names;
      vector<unique_ptr<Shortcut>> new_shortcuts;
      vector<QuickReplyShortcutId> changed_shortcut_ids;
      vector<QuickReplyShortcutId> deleted_shortcut_ids;
      for (auto &quick_reply : shortcuts->quick_replies_) {
        auto shortcut_id = QuickReplyShortcutId(quick_reply->shortcut_id_);
        if (!shortcut_id.is_server() || quick_reply->shortcut_.empty() || quick_reply->count_ <= 0 ||
            quick_reply->top_message_ <= 0) {
          LOG(ERROR) << "Receive " << to_string(quick_reply);
          continue;
        }
        if (added_shortcut_ids.count(shortcut_id) || added_shortcut_names.count(quick_reply->shortcut_)) {
          LOG(ERROR) << "Receive duplicate " << to_string(quick_reply);
          continue;
        }
        if (deleted_shortcut_ids_.count(shortcut_id)) {
          continue;
        }
        added_shortcut_ids.insert(shortcut_id);
        added_shortcut_names.insert(quick_reply->shortcut_);

        MessageId first_message_id(ServerMessageId(quick_reply->top_message_));
        auto it = message_id_to_message.find(first_message_id);
        if (it == message_id_to_message.end()) {
          LOG(ERROR) << "Can't find last " << first_message_id << " in " << shortcut_id;
          continue;
        }
        auto message = create_message(std::move(it->second), "on_reload_quick_reply_shortcuts");
        message_id_to_message.erase(it);
        if (message == nullptr) {
          continue;
        }
        if (message->shortcut_id != shortcut_id) {
          LOG(ERROR) << "Receive message from " << message->shortcut_id << " instead of " << shortcut_id;
          continue;
        }

        auto shortcut = td::make_unique<Shortcut>();
        shortcut->name_ = std::move(quick_reply->shortcut_);
        shortcut->shortcut_id_ = shortcut_id;
        shortcut->server_total_count_ = quick_reply->count_;
        shortcut->messages_.push_back(std::move(message));

        auto old_shortcut = get_shortcut(shortcut_id);
        auto is_object_changed = false;
        if (old_shortcut == nullptr || update_shortcut_from(shortcut.get(), old_shortcut, true, &is_object_changed)) {
          if (old_shortcut == nullptr || is_object_changed) {
            changed_shortcut_ids.push_back(shortcut_id);
          }
        }
        old_shortcut_ids.erase(shortcut_id);

        new_shortcuts.push_back(std::move(shortcut));
      }
      for (auto shortcut_id : old_shortcut_ids) {
        auto old_shortcut = get_shortcut(shortcut_id);
        CHECK(old_shortcut != nullptr);
        auto is_changed = td::remove_if(old_shortcut->messages_, [](const unique_ptr<QuickReplyMessage> &message) {
          return message->message_id.is_server();
        });
        if (old_shortcut->messages_.empty()) {
          CHECK(is_changed);
          send_update_quick_reply_shortcut_deleted(old_shortcut);
        } else {
          // some local messages has left
          if (added_shortcut_names.count(old_shortcut->name_)) {
            // a local shortcut has been created server-side
            for (auto &shortcut : new_shortcuts) {
              if (shortcut->name_ == old_shortcut->name_) {
                CHECK(shortcut->local_total_count_ == 0);
                shortcut->local_total_count_ = static_cast<int32>(old_shortcut->messages_.size());
                append(shortcut->messages_, std::move(old_shortcut->messages_));
                sort_quick_reply_messages(shortcut->messages_);
                send_update_quick_reply_shortcut_deleted(old_shortcut);
                if (!td::contains(changed_shortcut_ids, shortcut->shortcut_id_)) {
                  changed_shortcut_ids.push_back(shortcut->shortcut_id_);
                }
              }
            }
            continue;
          }

          auto shortcut = td::make_unique<Shortcut>();
          shortcut->name_ = std::move(old_shortcut->name_);
          shortcut->shortcut_id_ = old_shortcut->shortcut_id_;
          shortcut->server_total_count_ = 0;
          shortcut->local_total_count_ = static_cast<int32>(old_shortcut->messages_.size());
          shortcut->messages_ = std::move(old_shortcut->messages_);
          if (is_changed) {
            send_update_quick_reply_shortcut(shortcut.get(), "on_reload_quick_reply_shortcuts 1");
          }
          new_shortcuts.push_back(std::move(shortcut));
        }
      }
      bool is_list_changed = is_shortcut_list_changed(new_shortcuts);
      shortcuts_.shortcuts_ = std::move(new_shortcuts);
      shortcuts_.are_inited_ = true;

      save_quick_reply_shortcuts();
      for (auto shortcut_id : changed_shortcut_ids) {
        send_update_quick_reply_shortcut(get_shortcut(shortcut_id), "on_reload_quick_reply_shortcuts 2");
      }
      if (is_list_changed) {
        send_update_quick_reply_shortcuts();
      }
      break;
    }
  }
  on_load_quick_reply_success();
}

bool QuickReplyManager::is_shortcut_list_changed(const vector<unique_ptr<Shortcut>> &new_shortcuts) const {
  if (!shortcuts_.are_inited_ || shortcuts_.shortcuts_.size() != new_shortcuts.size()) {
    return true;
  }
  for (size_t i = 0; i < new_shortcuts.size(); i++) {
    if (shortcuts_.shortcuts_[i]->shortcut_id_ != new_shortcuts[i]->shortcut_id_) {
      return true;
    }
  }
  return false;
}

void QuickReplyManager::on_load_quick_reply_success() {
  set_promises(shortcuts_.load_queries_);
}

void QuickReplyManager::on_load_quick_reply_fail(Status error) {
  fail_promises(shortcuts_.load_queries_, std::move(error));
}

int64 QuickReplyManager::get_shortcuts_hash() const {
  vector<uint64> numbers;
  for (auto &shortcut : shortcuts_.shortcuts_) {
    for (auto &message : shortcut->messages_) {
      if (message->message_id.is_server()) {
        numbers.push_back(shortcut->shortcut_id_.get());
        numbers.push_back(get_md5_string_hash(shortcut->name_));
        numbers.push_back(message->message_id.get_server_message_id().get());
        numbers.push_back(message->edit_date);
        break;
      }
    }
  }
  return get_vector_hash(numbers);
}

void QuickReplyManager::delete_quick_reply_shortcut(QuickReplyShortcutId shortcut_id, Promise<Unit> &&promise) {
  auto it = get_shortcut_it(shortcut_id);
  if (it == shortcuts_.shortcuts_.end()) {
    return promise.set_error(Status::Error(400, "Shortcut not found"));
  }
  shortcuts_.shortcuts_.erase(it);
  save_quick_reply_shortcuts();

  if (!shortcut_id.is_server()) {
    return promise.set_value(Unit());
  }

  delete_quick_reply_shortcut_from_server(shortcut_id, std::move(promise));
}

void QuickReplyManager::delete_quick_reply_shortcut_from_server(QuickReplyShortcutId shortcut_id,
                                                                Promise<Unit> &&promise) {
  CHECK(shortcut_id.is_server());

  deleted_shortcut_ids_.insert(shortcut_id);
  td_->create_handler<DeleteQuickReplyShortcutQuery>(std::move(promise))->send(shortcut_id);
}

void QuickReplyManager::reorder_quick_reply_shortcuts(const vector<QuickReplyShortcutId> &shortcut_ids,
                                                      Promise<Unit> &&promise) {
  FlatHashSet<QuickReplyShortcutId, QuickReplyShortcutIdHash> unique_shortcut_ids;
  for (const auto &shortcut_id : shortcut_ids) {
    if (get_shortcut(shortcut_id) == nullptr) {
      return promise.set_error(Status::Error(400, "Shortcut not found"));
    }
    unique_shortcut_ids.insert(shortcut_id);
  }
  if (unique_shortcut_ids.size() != shortcut_ids.size()) {
    return promise.set_error(Status::Error(400, "Duplicate shortcut identifiers specified"));
  }
  if (!shortcuts_.are_inited_) {
    return promise.set_value(Unit());
  }
  auto old_shortcut_ids = get_shortcut_ids();
  auto old_server_shortcut_ids = get_server_shortcut_ids();
  vector<unique_ptr<Shortcut>> shortcuts;
  for (const auto &shortcut_id : shortcut_ids) {
    auto it = get_shortcut_it(shortcut_id);
    CHECK(it != shortcuts_.shortcuts_.end() && *it != nullptr);
    shortcuts.push_back(std::move(*it));
  }
  for (auto &shortcut : shortcuts_.shortcuts_) {
    if (shortcut != nullptr) {
      CHECK(unique_shortcut_ids.count(shortcut->shortcut_id_) == 0);
      shortcuts.push_back(std::move(shortcut));
    }
  }
  shortcuts_.shortcuts_ = std::move(shortcuts);
  if (old_shortcut_ids == get_shortcut_ids()) {
    return promise.set_value(Unit());
  }
  save_quick_reply_shortcuts();
  send_update_quick_reply_shortcuts();

  auto new_server_shortcut_ids = get_server_shortcut_ids();
  if (new_server_shortcut_ids == old_server_shortcut_ids) {
    return promise.set_value(Unit());
  }

  reorder_quick_reply_shortcuts_on_server(new_server_shortcut_ids, std::move(promise));
}

void QuickReplyManager::reorder_quick_reply_shortcuts_on_server(vector<QuickReplyShortcutId> shortcut_ids,
                                                                Promise<Unit> &&promise) {
  td_->create_handler<ReorderQuickRepliesQuery>(std::move(promise))->send(std::move(shortcut_ids));
}

QuickReplyManager::Shortcut *QuickReplyManager::get_shortcut(QuickReplyShortcutId shortcut_id) {
  if (!shortcuts_.are_inited_) {
    return nullptr;
  }
  for (auto &shortcut : shortcuts_.shortcuts_) {
    if (shortcut->shortcut_id_ == shortcut_id) {
      return shortcut.get();
    }
  }
  return nullptr;
}

QuickReplyManager::Shortcut *QuickReplyManager::get_shortcut(const string &name) {
  if (!shortcuts_.are_inited_) {
    return nullptr;
  }
  for (auto &shortcut : shortcuts_.shortcuts_) {
    if (shortcut->name_ == name) {
      return shortcut.get();
    }
  }
  return nullptr;
}

vector<unique_ptr<QuickReplyManager::Shortcut>>::iterator QuickReplyManager::get_shortcut_it(
    QuickReplyShortcutId shortcut_id) {
  for (auto it = shortcuts_.shortcuts_.begin(); it != shortcuts_.shortcuts_.end(); ++it) {
    if ((*it)->shortcut_id_ == shortcut_id) {
      return it;
    }
  }
  return shortcuts_.shortcuts_.end();
}

vector<unique_ptr<QuickReplyManager::Shortcut>>::iterator QuickReplyManager::get_shortcut_it(const string &name) {
  for (auto it = shortcuts_.shortcuts_.begin(); it != shortcuts_.shortcuts_.end(); ++it) {
    if ((*it)->name_ == name) {
      return it;
    }
  }
  return shortcuts_.shortcuts_.end();
}

vector<QuickReplyShortcutId> QuickReplyManager::get_shortcut_ids() const {
  return transform(shortcuts_.shortcuts_, [](const unique_ptr<Shortcut> &shortcut) { return shortcut->shortcut_id_; });
}

vector<QuickReplyShortcutId> QuickReplyManager::get_server_shortcut_ids() const {
  vector<QuickReplyShortcutId> shortcut_ids;
  for (auto &shortcut : shortcuts_.shortcuts_) {
    if (shortcut->shortcut_id_.is_server()) {
      shortcut_ids.push_back(shortcut->shortcut_id_);
    }
  }
  return shortcut_ids;
}

void QuickReplyManager::sort_quick_reply_messages(vector<unique_ptr<QuickReplyMessage>> &messages) {
  std::sort(messages.begin(), messages.end(),
            [](const unique_ptr<QuickReplyMessage> &lhs, const unique_ptr<QuickReplyMessage> &rhs) {
              return lhs->message_id < rhs->message_id;
            });
}

QuickReplyManager::QuickReplyMessageUniqueId QuickReplyManager::get_quick_reply_unique_id(const QuickReplyMessage *m) {
  return QuickReplyMessageUniqueId(m->message_id, m->edit_date);
}

vector<QuickReplyManager::QuickReplyMessageUniqueId> QuickReplyManager::get_quick_reply_unique_ids(
    const vector<unique_ptr<QuickReplyMessage>> &messages) {
  return transform(
      messages, [](const unique_ptr<QuickReplyMessage> &message) { return get_quick_reply_unique_id(message.get()); });
}

vector<QuickReplyManager::QuickReplyMessageUniqueId> QuickReplyManager::get_server_quick_reply_unique_ids(
    const vector<unique_ptr<QuickReplyMessage>> &messages) {
  auto message_ids = get_quick_reply_unique_ids(messages);
  td::remove_if(message_ids, [](const QuickReplyMessageUniqueId &message_id) { return !message_id.first.is_server(); });
  return message_ids;
}

bool QuickReplyManager::update_shortcut_from(Shortcut *new_shortcut, Shortcut *old_shortcut, bool is_partial,
                                             bool *is_object_changed) {
  CHECK(old_shortcut != nullptr);
  CHECK(new_shortcut != nullptr);
  CHECK(old_shortcut->shortcut_id_ == new_shortcut->shortcut_id_);
  CHECK(!old_shortcut->messages_.empty());
  CHECK(!new_shortcut->messages_.empty());
  auto old_unique_id = get_quick_reply_unique_id(old_shortcut->messages_[0].get());
  auto old_message_count = get_shortcut_message_count(old_shortcut);
  bool is_changed = false;
  if (is_partial) {
    // only the first server message is known
    // delete all definitely deleted server messages and insert the new message in the correct place
    auto old_message_ids = get_quick_reply_unique_ids(old_shortcut->messages_);
    auto new_first_message_id = new_shortcut->messages_[0]->message_id;
    auto it = old_shortcut->messages_.begin();
    while (it != old_shortcut->messages_.end() && (*it)->message_id < new_first_message_id) {
      if ((*it)->message_id.is_server()) {
        it = old_shortcut->messages_.erase(it);
      } else {
        ++it;
      }
    }
    if (it == old_shortcut->messages_.end() || (*it)->message_id != new_first_message_id) {
      old_shortcut->messages_.insert(it, std::move(new_shortcut->messages_[0]));
    } else {
      *it = std::move(new_shortcut->messages_[0]);
    }
    new_shortcut->messages_ = std::move(old_shortcut->messages_);
    is_changed = (old_message_ids != get_quick_reply_unique_ids(new_shortcut->messages_));
  } else {
    auto old_server_message_ids = get_server_quick_reply_unique_ids(old_shortcut->messages_);
    auto new_server_message_ids = get_server_quick_reply_unique_ids(new_shortcut->messages_);
    if (old_server_message_ids == new_server_message_ids) {
      new_shortcut->messages_ = std::move(old_shortcut->messages_);
    } else {
      is_changed = true;
      for (auto &message : old_shortcut->messages_) {
        if (!message->message_id.is_server()) {
          new_shortcut->messages_.push_back(std::move(message));
        }
      }
      sort_quick_reply_messages(new_shortcut->messages_);
    }
  }
  *is_object_changed = old_unique_id != get_quick_reply_unique_id(new_shortcut->messages_[0].get()) ||
                       new_shortcut->name_ != old_shortcut->name_ ||
                       old_message_count != get_shortcut_message_count(new_shortcut);
  return *is_object_changed || is_changed || old_shortcut->server_total_count_ != new_shortcut->server_total_count_;
}

string QuickReplyManager::get_quick_reply_shortcuts_database_key() {
  return "quick_reply_shortcuts";
}

void QuickReplyManager::save_quick_reply_shortcuts() {
  G()->td_db()->get_binlog_pmc()->set(get_quick_reply_shortcuts_database_key(),
                                      log_event_store(shortcuts_).as_slice().str());
}

td_api::object_ptr<td_api::updateQuickReplyShortcut> QuickReplyManager::get_update_quick_reply_shortcut_object(
    const Shortcut *s, const char *source) const {
  return td_api::make_object<td_api::updateQuickReplyShortcut>(get_quick_reply_shortcut_object(s, source));
}

void QuickReplyManager::send_update_quick_reply_shortcut(const Shortcut *s, const char *source) {
  send_closure(G()->td(), &Td::send_update, get_update_quick_reply_shortcut_object(s, source));
}

td_api::object_ptr<td_api::updateQuickReplyShortcutDeleted>
QuickReplyManager::get_update_quick_reply_shortcut_deleted_object(const Shortcut *s) const {
  CHECK(s != nullptr);
  return td_api::make_object<td_api::updateQuickReplyShortcutDeleted>(s->shortcut_id_.get());
}

void QuickReplyManager::send_update_quick_reply_shortcut_deleted(const Shortcut *s) {
  send_closure(G()->td(), &Td::send_update, get_update_quick_reply_shortcut_deleted_object(s));
}

td_api::object_ptr<td_api::updateQuickReplyShortcuts> QuickReplyManager::get_update_quick_reply_shortcuts_object()
    const {
  CHECK(shortcuts_.are_inited_);
  return td_api::make_object<td_api::updateQuickReplyShortcuts>(transform(
      shortcuts_.shortcuts_, [](const unique_ptr<Shortcut> &shortcut) { return shortcut->shortcut_id_.get(); }));
}

void QuickReplyManager::send_update_quick_reply_shortcuts() {
  send_closure(G()->td(), &Td::send_update, get_update_quick_reply_shortcuts_object());
}

void QuickReplyManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  for (auto &shortcut : shortcuts_.shortcuts_) {
    updates.push_back(get_update_quick_reply_shortcut_object(shortcut.get(), "get_current_state"));
  }

  if (shortcuts_.are_inited_) {
    updates.push_back(get_update_quick_reply_shortcuts_object());
  }
}

}  // namespace td
