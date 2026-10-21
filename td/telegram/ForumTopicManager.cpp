//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ForumTopicManager.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/AuthManager.h"
#include "td/telegram/ChannelId.h"
#include "td/telegram/ChatManager.h"
#include "td/telegram/CustomEmojiId.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/DraftMessageManager.h"
#include "td/telegram/ForumTopic.h"
#include "td/telegram/ForumTopic.hpp"
#include "td/telegram/ForumTopicIcon.h"
#include "td/telegram/ForumTopicInfo.hpp"
#include "td/telegram/Global.h"
#include "td/telegram/LinkManager.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/MessageQueryManager.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/MessageThreadDb.h"
#include "td/telegram/MessageTopic.h"
#include "td/telegram/misc.h"
#include "td/telegram/NotificationManager.h"
#include "td/telegram/NotificationSettingsManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UpdatesManager.h"
#include "td/telegram/UserManager.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"
#include "td/utils/tl_helpers.h"

namespace td {

class CreateForumTopicQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::forumTopicInfo>> promise_;
  DialogId dialog_id_;
  DialogId creator_dialog_id_;
  int64 random_id_;

 public:
  explicit CreateForumTopicQuery(Promise<td_api::object_ptr<td_api::forumTopicInfo>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, const string &title, bool title_missing, int32 icon_color,
            CustomEmojiId icon_custom_emoji_id, DialogId as_dialog_id) {
    dialog_id_ = dialog_id;
    creator_dialog_id_ = td_->dialog_manager_->get_my_dialog_id();

    int32 flags = 0;
    if (icon_color != -1) {
      flags |= telegram_api::messages_createForumTopic::ICON_COLOR_MASK;
    }
    if (icon_custom_emoji_id.is_valid()) {
      flags |= telegram_api::messages_createForumTopic::ICON_EMOJI_ID_MASK;
    }
    telegram_api::object_ptr<telegram_api::InputPeer> as_input_peer;
    if (as_dialog_id.is_valid()) {
      as_input_peer = td_->dialog_manager_->get_input_peer(as_dialog_id, AccessRights::Write);
      if (as_input_peer != nullptr) {
        creator_dialog_id_ = as_dialog_id;
      }
    }

    do {
      random_id_ = Random::secure_int64();
    } while (random_id_ == 0);

    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Write);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }
    send_query(G()->net_query_creator().create(
        telegram_api::messages_createForumTopic(flags, title_missing, std::move(input_peer), title, icon_color,
                                                icon_custom_emoji_id.get(), random_id_, std::move(as_input_peer)),
        {{dialog_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_createForumTopic>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for CreateForumTopicQuery: " << to_string(ptr);
    auto message = UpdatesManager::get_message_by_random_id(ptr.get(), dialog_id_, random_id_);
    if (message == nullptr || message->get_id() != telegram_api::messageService::ID) {
      LOG(ERROR) << "Receive invalid result for CreateForumTopicQuery: " << to_string(ptr);
      return promise_.set_error(400, "Invalid result received");
    }
    auto service_message = static_cast<const telegram_api::messageService *>(message);
    if (service_message->action_->get_id() != telegram_api::messageActionTopicCreate::ID) {
      LOG(ERROR) << "Receive invalid result for CreateForumTopicQuery: " << to_string(ptr);
      return promise_.set_error(400, "Invalid result received");
    }

    auto action = static_cast<const telegram_api::messageActionTopicCreate *>(service_message->action_.get());
    auto forum_topic_id = ForumTopicId(service_message->id_);
    if (service_message->reply_to_ != nullptr &&
        service_message->reply_to_->get_id() == telegram_api::messageReplyHeader::ID) {
      auto reply_header = static_cast<const telegram_api::messageReplyHeader *>(service_message->reply_to_.get());
      if (reply_header->forum_topic_ && reply_header->reply_to_top_id_ > 0) {
        forum_topic_id = ForumTopicId(reply_header->reply_to_top_id_);
      }
    }
    auto forum_topic_info = td::make_unique<ForumTopicInfo>(
        dialog_id_, forum_topic_id, action->title_, ForumTopicIcon(action->icon_color_, action->icon_emoji_id_),
        service_message->date_, creator_dialog_id_, true, false, false, action->title_missing_);
    td_->updates_manager_->on_get_updates(
        std::move(ptr), PromiseCreator::lambda([dialog_id = dialog_id_, forum_topic_info = std::move(forum_topic_info),
                                                promise = std::move(promise_)](Unit result) mutable {
          send_closure(G()->forum_topic_manager(), &ForumTopicManager::on_forum_topic_created, dialog_id,
                       std::move(forum_topic_info), std::move(promise));
        }));
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "CreateForumTopicQuery");
    promise_.set_error(std::move(status));
  }
};

class EditForumTopicQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  DialogId dialog_id_;
  ForumTopicId forum_topic_id_;

 public:
  explicit EditForumTopicQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, ForumTopicId forum_topic_id, const string &title, bool edit_custom_emoji_id,
            CustomEmojiId icon_custom_emoji_id) {
    dialog_id_ = dialog_id;
    forum_topic_id_ = forum_topic_id;

    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Write);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }

    int32 flags = 0;
    if (edit_custom_emoji_id) {
      flags |= telegram_api::messages_editForumTopic::ICON_EMOJI_ID_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::messages_editForumTopic(flags, std::move(input_peer), forum_topic_id.get(), title,
                                              icon_custom_emoji_id.get(), false, false),
        {{dialog_id}}));
  }

  void send(ChannelId channel_id, ForumTopicId forum_topic_id, bool is_closed) {
    dialog_id_ = DialogId(channel_id);
    forum_topic_id_ = forum_topic_id;

    auto input_peer = td_->dialog_manager_->get_input_peer(DialogId(channel_id), AccessRights::Write);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }

    int32 flags = telegram_api::messages_editForumTopic::CLOSED_MASK;
    send_query(G()->net_query_creator().create(
        telegram_api::messages_editForumTopic(flags, std::move(input_peer), forum_topic_id.get(), string(), 0,
                                              is_closed, false),
        {{channel_id}}));
  }

  void send(ChannelId channel_id, bool is_hidden) {
    dialog_id_ = DialogId(channel_id);
    forum_topic_id_ = ForumTopicId::general();

    auto input_peer = td_->dialog_manager_->get_input_peer(DialogId(channel_id), AccessRights::Write);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }

    int32 flags = telegram_api::messages_editForumTopic::HIDDEN_MASK;
    send_query(G()->net_query_creator().create(
        telegram_api::messages_editForumTopic(flags, std::move(input_peer), forum_topic_id_.get(), string(), 0, false,
                                              is_hidden),
        {{channel_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_editForumTopic>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for EditForumTopicQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    if (status.message() == "TOPIC_NOT_MODIFIED" && !td_->auth_manager_->is_bot()) {
      return promise_.set_value(Unit());
    }
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "EditForumTopicQuery");
    promise_.set_error(std::move(status));
  }
};

class UpdatePinnedForumTopicQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  DialogId dialog_id_;

 public:
  explicit UpdatePinnedForumTopicQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, ForumTopicId forum_topic_id, bool is_pinned) {
    dialog_id_ = dialog_id;

    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Write);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }

    send_query(G()->net_query_creator().create(
        telegram_api::messages_updatePinnedForumTopic(std::move(input_peer), forum_topic_id.get(), is_pinned),
        {{dialog_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_updatePinnedForumTopic>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for UpdatePinnedForumTopicQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    if (status.message() == "PINNED_TOPIC_NOT_MODIFIED" && !td_->auth_manager_->is_bot()) {
      return promise_.set_value(Unit());
    }
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "UpdatePinnedForumTopicQuery");
    promise_.set_error(std::move(status));
  }
};

class ReorderPinnedForumTopicsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  DialogId dialog_id_;

 public:
  explicit ReorderPinnedForumTopicsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, const vector<ForumTopicId> &forum_topic_ids) {
    dialog_id_ = dialog_id;

    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Write);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }

    send_query(G()->net_query_creator().create(
        telegram_api::messages_reorderPinnedForumTopics(0, true, std::move(input_peer),
                                                        ForumTopicId::get_top_msg_ids(forum_topic_ids)),
        {{dialog_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_reorderPinnedForumTopics>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for ReorderPinnedForumTopicsQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    if (status.message() == "PINNED_TOPICS_NOT_MODIFIED" && !td_->auth_manager_->is_bot()) {
      return promise_.set_value(Unit());
    }
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "ReorderPinnedForumTopicsQuery");
    promise_.set_error(std::move(status));
  }
};

class GetForumTopicQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::forumTopic>> promise_;
  DialogId dialog_id_;
  ForumTopicId forum_topic_id_;

 public:
  explicit GetForumTopicQuery(Promise<td_api::object_ptr<td_api::forumTopic>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, ForumTopicId forum_topic_id) {
    dialog_id_ = dialog_id;
    forum_topic_id_ = forum_topic_id;

    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }

    send_query(G()->net_query_creator().create(
        telegram_api::messages_getForumTopicsByID(std::move(input_peer), {forum_topic_id.get()}), {{dialog_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getForumTopicsByID>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetForumTopicQuery: " << to_string(ptr);

    td_->user_manager_->on_get_users(std::move(ptr->users_), "GetForumTopicQuery");
    td_->chat_manager_->on_get_chats(std::move(ptr->chats_), "GetForumTopicQuery");

    if (ptr->topics_.size() != 1u) {
      return promise_.set_value(nullptr);
    }

    MessagesInfo messages_info;
    messages_info.messages = std::move(ptr->messages_);
    messages_info.total_count = ptr->count_;
    messages_info.is_channel_messages = dialog_id_.get_type() == DialogType::Channel;

    td_->messages_manager_->get_channel_difference_if_needed(
        dialog_id_, std::move(messages_info),
        PromiseCreator::lambda([actor_id = td_->forum_topic_manager_actor_.get(), dialog_id = dialog_id_,
                                forum_topic_id = forum_topic_id_, topic = std::move(ptr->topics_[0]),
                                promise = std::move(promise_)](Result<MessagesInfo> &&result) mutable {
          if (result.is_error()) {
            promise.set_error(result.move_as_error());
          } else {
            auto info = result.move_as_ok();
            send_closure(actor_id, &ForumTopicManager::on_get_forum_topic, dialog_id, forum_topic_id, std::move(info),
                         std::move(topic), std::move(promise));
          }
        }),
        "GetForumTopicQuery");
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "GetForumTopicQuery");
    promise_.set_error(std::move(status));
  }
};

class GetForumTopicsByIdQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  DialogId dialog_id_;

 public:
  explicit GetForumTopicsByIdQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, const vector<ForumTopicId> &forum_topic_ids) {
    dialog_id_ = dialog_id;

    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }

    send_query(
        G()->net_query_creator().create(telegram_api::messages_getForumTopicsByID(
                                            std::move(input_peer), ForumTopicId::get_top_msg_ids(forum_topic_ids)),
                                        {{dialog_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getForumTopicsByID>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetForumTopicsByIdQuery: " << to_string(ptr);

    td_->user_manager_->on_get_users(std::move(ptr->users_), "GetForumTopicsByIdQuery");
    td_->chat_manager_->on_get_chats(std::move(ptr->chats_), "GetForumTopicsByIdQuery");

    MessagesInfo messages_info;
    messages_info.messages = std::move(ptr->messages_);
    messages_info.total_count = ptr->count_;
    messages_info.is_channel_messages = dialog_id_.get_type() == DialogType::Channel;

    td_->messages_manager_->get_channel_difference_if_needed(
        dialog_id_, std::move(messages_info),
        PromiseCreator::lambda([actor_id = td_->forum_topic_manager_actor_.get(), dialog_id = dialog_id_,
                                topics = std::move(ptr->topics_),
                                promise = std::move(promise_)](Result<MessagesInfo> &&result) mutable {
          if (result.is_error()) {
            promise.set_error(result.move_as_error());
          } else {
            send_closure(actor_id, &ForumTopicManager::on_get_forum_topics_by_id, dialog_id, result.move_as_ok(),
                         std::move(topics), std::move(promise));
          }
        }),
        "GetForumTopicsByIdQuery");
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "GetForumTopicsByIdQuery");
    promise_.set_error(std::move(status));
  }
};

class GetForumTopicsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::forumTopics>> promise_;
  DialogId dialog_id_;
  uint32 generation_ = 0;

 public:
  explicit GetForumTopicsQuery(Promise<td_api::object_ptr<td_api::forumTopics>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, uint32 generation, const string &query, int32 offset_date, MessageId offset_message_id,
            ForumTopicId offset_forum_topic_id, int32 limit) {
    dialog_id_ = dialog_id;
    generation_ = generation;

    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }

    send_query(G()->net_query_creator().create(
        telegram_api::messages_getForumTopics(0, std::move(input_peer), query, offset_date,
                                              offset_message_id.get_server_message_id().get(),
                                              offset_forum_topic_id.get(), limit),
        {{dialog_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getForumTopics>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetForumTopicsQuery: " << to_string(ptr);

    td_->user_manager_->on_get_users(std::move(ptr->users_), "GetForumTopicsQuery");
    td_->chat_manager_->on_get_chats(std::move(ptr->chats_), "GetForumTopicsQuery");

    MessagesInfo messages_info;
    messages_info.messages = std::move(ptr->messages_);
    messages_info.total_count = ptr->count_;
    messages_info.is_channel_messages = dialog_id_.get_type() == DialogType::Channel;

    // ignore ptr->pts_
    td_->messages_manager_->get_channel_difference_if_needed(
        dialog_id_, std::move(messages_info),
        PromiseCreator::lambda([actor_id = td_->forum_topic_manager_actor_.get(), dialog_id = dialog_id_,
                                generation = generation_, order_by_creation_date = ptr->order_by_create_date_,
                                topics = std::move(ptr->topics_),
                                promise = std::move(promise_)](Result<MessagesInfo> &&result) mutable {
          if (result.is_error()) {
            promise.set_error(result.move_as_error());
          } else {
            auto info = result.move_as_ok();
            send_closure(actor_id, &ForumTopicManager::on_get_forum_topics, dialog_id, generation,
                         order_by_creation_date, std::move(info), std::move(topics), std::move(promise));
          }
        }),
        "GetForumTopicsQuery");
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "GetForumTopicsQuery");
    promise_.set_error(std::move(status));
  }
};

class ReadForumTopicQuery final : public Td::ResultHandler {
  DialogId dialog_id_;

 public:
  void send(DialogId dialog_id, ForumTopicId forum_topic_id, MessageId max_message_id) {
    dialog_id_ = dialog_id;
    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }
    send_query(G()->net_query_creator().create(
        telegram_api::messages_readDiscussion(std::move(input_peer), forum_topic_id.get(),
                                              max_message_id.get_server_message_id().get()),
        {{dialog_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_readDiscussion>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "ReadForumTopicQuery");
  }
};

template <class StorerT>
void ForumTopicManager::Topic::store(StorerT &storer) const {
  CHECK(info_ != nullptr);
  using td::store;

  store(MAGIC, storer);

  bool has_topic = topic_ != nullptr;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_topic);
  END_STORE_FLAGS();
  store(info_, storer);
  if (has_topic) {
    store(topic_, storer);
  }
}

template <class ParserT>
void ForumTopicManager::Topic::parse(ParserT &parser) {
  CHECK(info_ != nullptr);
  using td::parse;

  int32 magic;
  parse(magic, parser);
  if (magic != MAGIC) {
    return parser.set_error("Invalid magic");
  }

  bool has_topic;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_topic);
  END_PARSE_FLAGS();
  parse(info_, parser);
  if (has_topic) {
    parse(topic_, parser);
  }
}

ForumTopicManager::ForumTopicManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  reload_topics_timeout_.set_callback(on_reload_topics_timeout_callback);
  reload_topics_timeout_.set_callback_data(static_cast<void *>(this));
}

ForumTopicManager::~ForumTopicManager() {
  Scheduler::instance()->destroy_on_scheduler(G()->get_gc_scheduler_id(), dialog_topics_);
}

void ForumTopicManager::tear_down() {
  parent_.reset();
}

void ForumTopicManager::create_forum_topic(DialogId dialog_id, string &&title, bool title_missing,
                                           td_api::object_ptr<td_api::forumTopicIcon> &&icon,
                                           Promise<td_api::object_ptr<td_api::forumTopicInfo>> &&promise) {
  TRY_STATUS_PROMISE(promise, is_forum(dialog_id, true));
  if (dialog_id.get_type() == DialogType::Channel) {
    auto channel_id = dialog_id.get_channel_id();

    if (!td_->chat_manager_->get_channel_permissions(channel_id).can_create_topics()) {
      return promise.set_error(400, "Not enough rights to create a topic");
    }
  }

  auto new_title = clean_name(std::move(title), MAX_FORUM_TOPIC_TITLE_LENGTH);
  if (new_title.empty()) {
    return promise.set_error(400, "Name must be non-empty");
  }

  int32 icon_color = -1;
  CustomEmojiId icon_custom_emoji_id;
  if (icon != nullptr) {
    icon_color = icon->color_;
    if (icon_color < 0 || icon_color > 0xFFFFFF) {
      return promise.set_error(400, "Invalid icon color specified");
    }
    icon_custom_emoji_id = CustomEmojiId(icon->custom_emoji_id_);
  }

  DialogId as_dialog_id = td_->messages_manager_->get_dialog_default_send_message_as_dialog_id(dialog_id);

  td_->create_handler<CreateForumTopicQuery>(std::move(promise))
      ->send(dialog_id, new_title, title_missing, icon_color, icon_custom_emoji_id, as_dialog_id);
}

void ForumTopicManager::on_forum_topic_created(DialogId dialog_id, unique_ptr<ForumTopicInfo> &&forum_topic_info,
                                               Promise<td_api::object_ptr<td_api::forumTopicInfo>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  CHECK(forum_topic_info != nullptr);
  ForumTopicId forum_topic_id = forum_topic_info->get_forum_topic_id();
  auto topic = add_topic(dialog_id, forum_topic_id);
  if (topic == nullptr) {
    return promise.set_value(forum_topic_info->get_forum_topic_info_object(td_));
  }
  if (topic->info_ == nullptr) {
    set_topic_info(dialog_id, topic, std::move(forum_topic_info));
  }
  save_topic_to_database(dialog_id, topic);
  promise.set_value(topic->info_->get_forum_topic_info_object(td_));
}

void ForumTopicManager::edit_forum_topic(DialogId dialog_id, ForumTopicId forum_topic_id, string &&title,
                                         bool edit_icon_custom_emoji, CustomEmojiId icon_custom_emoji_id,
                                         Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, is_forum(dialog_id, true));
  TRY_STATUS_PROMISE(promise, can_be_forum_topic_id(forum_topic_id));
  if (dialog_id.get_type() == DialogType::Channel) {
    auto channel_id = dialog_id.get_channel_id();

    if (!td_->chat_manager_->get_channel_permissions(channel_id).can_edit_topics()) {
      auto topic_info = get_topic_info(dialog_id, forum_topic_id);
      if (topic_info != nullptr && !topic_info->is_outgoing()) {
        return promise.set_error(400, "Not enough rights to edit the topic");
      }
    }
  }

  bool edit_title = !title.empty();
  auto new_title = clean_name(std::move(title), MAX_FORUM_TOPIC_TITLE_LENGTH);
  if (edit_title && new_title.empty()) {
    return promise.set_error(400, "Name must be non-empty");
  }
  if (!edit_title && !edit_icon_custom_emoji) {
    return promise.set_value(Unit());
  }

  td_->create_handler<EditForumTopicQuery>(std::move(promise))
      ->send(dialog_id, forum_topic_id, new_title, edit_icon_custom_emoji, icon_custom_emoji_id);
}

void ForumTopicManager::read_forum_topic_messages(DialogId dialog_id, ForumTopicId forum_topic_id,
                                                  MessageId last_read_inbox_message_id) {
  CHECK(!td_->auth_manager_->is_bot());
  auto topic = get_topic(dialog_id, forum_topic_id);
  if (topic == nullptr || topic->topic_ == nullptr) {
    return;
  }

  bool need_update = false;
  if (read_topic_messages(dialog_id, topic, last_read_inbox_message_id, -1)) {
    need_update = true;
    auto max_message_id = last_read_inbox_message_id.get_prev_server_message_id();
    LOG(INFO) << "Send read topic history request in topic of " << forum_topic_id << " in " << dialog_id << " up to "
              << max_message_id;
    td_->create_handler<ReadForumTopicQuery>()->send(dialog_id, forum_topic_id, max_message_id);
  }
  if (need_update) {
    on_forum_topic_changed(dialog_id, topic);
  }
}

void ForumTopicManager::on_update_forum_topic_unread(DialogId dialog_id, ForumTopicId forum_topic_id,
                                                     MessageId last_message_id, MessageId last_read_inbox_message_id,
                                                     MessageId last_read_outbox_message_id, int32 unread_count) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  auto topic = get_topic(dialog_id, forum_topic_id);
  if (topic == nullptr || topic->topic_ == nullptr) {
    return;
  }

  bool need_update = false;
  if (topic->topic_->update_last_read_outbox_message_id(last_read_outbox_message_id)) {
    need_update = true;
  }
  if (read_topic_messages(dialog_id, topic, last_read_inbox_message_id, unread_count)) {
    need_update = true;
  }
  if (need_update) {
    on_forum_topic_changed(dialog_id, topic);
  }
}

bool ForumTopicManager::read_topic_messages(DialogId dialog_id, Topic *topic, MessageId read_inbox_max_message_id,
                                            int32 hint_unread_count) {
  CHECK(topic != nullptr && topic->topic_ != nullptr);
  auto old_read_inbox_max_message_id = topic->topic_->get_last_read_inbox_message_id();
  if (read_inbox_max_message_id <= old_read_inbox_max_message_id) {
    return false;
  }
  auto unread_count = topic->ordered_messages_.calc_new_unread_count(
      read_inbox_max_message_id, old_read_inbox_max_message_id, topic->topic_->get_unread_count(),
      topic->topic_->get_last_message_id(),
      td_->messages_manager_->get_is_counted_as_unread(dialog_id, MessageType::Server), hint_unread_count);
  if (unread_count < 0) {
    // the messages between the old and the new read position aren't known; the server knows the count
    unread_count = hint_unread_count >= 0 ? hint_unread_count : topic->topic_->get_unread_count();
    if (hint_unread_count < 0) {
      reload_topic(dialog_id, get_dialog_topics(dialog_id), topic->info_->get_forum_topic_id());
    }
  }
  return topic->topic_->update_last_read_inbox_message_id(read_inbox_max_message_id, unread_count);
}

DialogNotificationSettings *ForumTopicManager::get_forum_topic_notification_settings(DialogId dialog_id,
                                                                                     ForumTopicId forum_topic_id) {
  auto topic = get_topic(dialog_id, forum_topic_id);
  if (topic == nullptr || topic->topic_ == nullptr) {
    return nullptr;
  }
  return topic->topic_->get_notification_settings();
}

const DialogNotificationSettings *ForumTopicManager::get_forum_topic_notification_settings(
    DialogId dialog_id, ForumTopicId forum_topic_id) const {
  auto topic = get_topic(dialog_id, forum_topic_id);
  if (topic == nullptr || topic->topic_ == nullptr) {
    return nullptr;
  }
  return topic->topic_->get_notification_settings();
}

void ForumTopicManager::on_update_forum_topic_notify_settings(
    DialogId dialog_id, ForumTopicId forum_topic_id,
    tl_object_ptr<telegram_api::peerNotifySettings> &&peer_notify_settings, const char *source) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  VLOG(notifications) << "Receive notification settings for topic of " << forum_topic_id << " in " << dialog_id
                      << " from " << source << ": " << to_string(peer_notify_settings);

  DialogNotificationSettings *current_settings = get_forum_topic_notification_settings(dialog_id, forum_topic_id);
  if (current_settings == nullptr) {
    return;
  }

  auto notification_settings = get_dialog_notification_settings(std::move(peer_notify_settings), current_settings);
  if (!notification_settings.is_synchronized) {
    return;
  }

  update_forum_topic_notification_settings(dialog_id, forum_topic_id, current_settings,
                                           std::move(notification_settings));
}

void ForumTopicManager::on_update_forum_topic_draft_message(DialogId dialog_id, ForumTopicId forum_topic_id,
                                                            unique_ptr<DraftMessage> &&draft_message) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }
  auto topic = get_topic(dialog_id, forum_topic_id);
  if (topic == nullptr || topic->topic_ == nullptr) {
    LOG(DEBUG) << "Ignore update about unknown " << forum_topic_id << " in " << dialog_id;
    return;
  }
  auto old_file_ids = get_topic_file_ids(topic);
  if (topic->topic_->set_draft_message(std::move(draft_message), true)) {
    on_forum_topic_changed(dialog_id, topic);
    td_->draft_message_manager_->change_draft_message_files(dialog_id, MessageTopic::forum(dialog_id, forum_topic_id),
                                                            old_file_ids, get_topic_file_ids(topic), true);
  }
}

void ForumTopicManager::clear_forum_topic_draft_by_sent_message(DialogId dialog_id, ForumTopicId forum_topic_id,
                                                                bool message_clear_draft,
                                                                MessageContentType message_content_type) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }
  LOG(INFO) << "Clear draft in " << forum_topic_id << " of " << dialog_id << " by sent message";
  auto topic = get_topic(dialog_id, forum_topic_id);
  if (topic == nullptr || topic->topic_ == nullptr) {
    return;
  }

  if (!message_clear_draft) {
    const auto *draft_message = topic->topic_->get_draft_message().get();
    if (draft_message == nullptr || !draft_message->need_clear_local(message_content_type)) {
      return;
    }
  }
  if (topic->topic_->set_draft_message(nullptr, false)) {
    on_forum_topic_changed(dialog_id, topic);
    // do not call change_draft_message_files to keep file source identifiers
  }
}

void ForumTopicManager::on_update_forum_topic_is_pinned(DialogId dialog_id, ForumTopicId forum_topic_id,
                                                        bool is_pinned) {
  if (!td_->dialog_manager_->have_dialog_force(dialog_id, "on_update_forum_topic_is_pinned")) {
    return;
  }
  if (!can_be_forum(dialog_id)) {
    LOG(ERROR) << "Receive pinned topics in " << dialog_id;
    return;
  }

  if (td_->auth_manager_->is_bot()) {
    return;
  }

  auto topic = get_topic(dialog_id, forum_topic_id);
  if (topic == nullptr || topic->topic_ == nullptr) {
    return;
  }
  if (topic->topic_->set_is_pinned(is_pinned)) {
    on_forum_topic_changed(dialog_id, topic);
  }
}

void ForumTopicManager::on_update_pinned_forum_topics(DialogId dialog_id, vector<ForumTopicId> forum_topic_ids) {
  if (!td_->dialog_manager_->have_dialog_force(dialog_id, "on_update_pinned_forum_topics")) {
    return;
  }
  if (!can_be_forum(dialog_id)) {
    LOG(ERROR) << "Receive pinned topics in " << dialog_id;
    return;
  }

  if (td_->auth_manager_->is_bot()) {
    return;
  }

  auto dialog_topics = get_dialog_topics(dialog_id);
  if (dialog_topics == nullptr) {
    return;
  }

  set_local_pinned_forum_topics(dialog_id, dialog_topics, std::move(forum_topic_ids));
}

Status ForumTopicManager::set_forum_topic_notification_settings(
    DialogId dialog_id, ForumTopicId forum_topic_id,
    td_api::object_ptr<td_api::chatNotificationSettings> &&notification_settings) {
  CHECK(!td_->auth_manager_->is_bot());
  TRY_STATUS(is_forum(dialog_id, true));
  TRY_STATUS(can_be_forum_topic_id(forum_topic_id));
  auto current_settings = get_forum_topic_notification_settings(dialog_id, forum_topic_id);
  if (current_settings == nullptr) {
    return Status::Error(400, "Unknown forum topic identifier specified");
  }

  TRY_RESULT(new_settings, get_dialog_notification_settings(std::move(notification_settings), current_settings));
  if (update_forum_topic_notification_settings(dialog_id, forum_topic_id, current_settings, std::move(new_settings))) {
    // TODO log event
    td_->notification_settings_manager_->update_dialog_notify_settings(dialog_id, forum_topic_id, *current_settings,
                                                                       Promise<Unit>());
  }
  return Status::OK();
}

bool ForumTopicManager::update_forum_topic_notification_settings(DialogId dialog_id, ForumTopicId forum_topic_id,
                                                                 DialogNotificationSettings *current_settings,
                                                                 DialogNotificationSettings &&new_settings) {
  if (td_->auth_manager_->is_bot()) {
    // just in case
    return false;
  }

  auto need_update = need_update_dialog_notification_settings(current_settings, new_settings);
  if (need_update.are_changed) {
    // TODO update unmute timeouts, remove notifications
    *current_settings = std::move(new_settings);

    auto topic = get_topic(dialog_id, forum_topic_id);
    on_forum_topic_changed(dialog_id, topic);
  }
  return need_update.need_update_server;
}

Status ForumTopicManager::set_forum_topic_draft_message(DialogId dialog_id, ForumTopicId forum_topic_id,
                                                        unique_ptr<DraftMessage> &&draft_message) {
  TRY_STATUS(is_forum(dialog_id, true));
  TRY_STATUS(can_be_forum_topic_id(forum_topic_id));
  auto topic = get_topic(dialog_id, forum_topic_id);
  if (topic == nullptr || topic->topic_ == nullptr) {
    td_->draft_message_manager_->save_draft_message(dialog_id, MessageTopic::forum(dialog_id, forum_topic_id),
                                                    std::move(draft_message), Promise<Unit>());
  } else if (topic->topic_->set_draft_message(std::move(draft_message), false)) {
    td_->draft_message_manager_->save_draft_message(dialog_id, MessageTopic::forum(dialog_id, forum_topic_id),
                                                    topic->topic_->get_draft_message(), Promise<Unit>());
    on_forum_topic_changed(dialog_id, topic);
  }
  return Status::OK();
}

void ForumTopicManager::get_forum_topic(DialogId dialog_id, ForumTopicId forum_topic_id,
                                        Promise<td_api::object_ptr<td_api::forumTopic>> &&promise) {
  TRY_STATUS_PROMISE(promise, is_forum(dialog_id, true));
  TRY_STATUS_PROMISE(promise, can_be_forum_topic_id(forum_topic_id));

  if (td_->auth_manager_->is_bot()) {
    auto topic = get_topic(dialog_id, forum_topic_id);
    if (topic != nullptr && topic->topic_ != nullptr && topic->receive_date_ > G()->unix_time() - 60) {
      CHECK(topic->info_ != nullptr);
      auto forum_topic = topic->topic_->get_forum_topic_object(td_, dialog_id, *topic->info_,
                                                               get_topic_public_order(dialog_id, topic));
      if (forum_topic != nullptr) {
        return promise.set_value(std::move(forum_topic));
      }
    }
  }

  td_->create_handler<GetForumTopicQuery>(std::move(promise))->send(dialog_id, forum_topic_id);
}

void ForumTopicManager::reload_forum_topic(DialogId dialog_id, ForumTopicId forum_topic_id, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, is_forum(dialog_id, true));
  TRY_STATUS_PROMISE(promise, can_be_forum_topic_id(forum_topic_id));

  auto query_promise = PromiseCreator::lambda(
      [promise = std::move(promise)](Result<td_api::object_ptr<td_api::forumTopic>> r_forum_topic) mutable {
        if (r_forum_topic.is_error()) {
          promise.set_error(r_forum_topic.move_as_error());
        } else {
          promise.set_value(Unit());
        }
      });
  td_->create_handler<GetForumTopicQuery>(std::move(query_promise))->send(dialog_id, forum_topic_id);
}

void ForumTopicManager::on_get_forum_topic(DialogId dialog_id, ForumTopicId expected_forum_topic_id,
                                           MessagesInfo &&info,
                                           telegram_api::object_ptr<telegram_api::ForumTopic> &&topic,
                                           Promise<td_api::object_ptr<td_api::forumTopic>> &&promise) {
  TRY_STATUS_PROMISE(promise, is_forum(dialog_id, true));
  on_get_topic_messages(dialog_id, std::move(info), {get_received_forum_topic_id(topic.get())}, "on_get_forum_topic");

  auto forum_topic_id = on_get_forum_topic_impl(dialog_id, std::move(topic), true);
  if (!forum_topic_id.is_valid()) {
    return promise.set_value(nullptr);
  }
  if (forum_topic_id != expected_forum_topic_id) {
    return promise.set_error(500, "Wrong forum topic received");
  }
  promise.set_value(get_forum_topic_object(dialog_id, forum_topic_id));
}

void ForumTopicManager::on_get_forum_topics_by_id(DialogId dialog_id, MessagesInfo &&info,
                                                  vector<telegram_api::object_ptr<telegram_api::ForumTopic>> &&topics,
                                                  Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, is_forum(dialog_id, true));
  on_get_topic_messages(dialog_id, std::move(info),
                        transform(topics, [](const auto &topic) { return get_received_forum_topic_id(topic.get()); }),
                        "on_get_forum_topics_by_id");
  for (auto &topic : topics) {
    on_get_forum_topic_impl(dialog_id, std::move(topic), true);
  }
  promise.set_value(Unit());
}

void ForumTopicManager::get_forum_topic_link(DialogId dialog_id, ForumTopicId forum_topic_id,
                                             Promise<td_api::object_ptr<td_api::messageLink>> &&promise) {
  TRY_STATUS_PROMISE(promise, is_forum(dialog_id));
  TRY_STATUS_PROMISE(promise, can_be_forum_topic_id(forum_topic_id));
  auto channel_id = dialog_id.get_channel_id();

  SliceBuilder sb;
  sb << LinkManager::get_t_me_url();

  bool is_public = false;
  auto dialog_username = td_->chat_manager_->get_channel_first_username(channel_id);
  if (!dialog_username.empty()) {
    sb << dialog_username;
    is_public = true;
  } else {
    sb << "c/" << channel_id.get();
  }
  sb << '/' << forum_topic_id.get();

  promise.set_value(td_api::make_object<td_api::messageLink>(sb.as_cslice().str(), is_public));
}

void ForumTopicManager::load_forum_topics(DialogId dialog_id, int32 limit, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, is_forum(dialog_id, true));
  if (limit < 0) {
    return promise.set_error(400, "Limit must be non-negative");
  }
  if (limit == 0) {
    return promise.set_value(Unit());
  }
  auto *dialog_topics = add_dialog_topics(dialog_id);
  if (dialog_topics->ordered_topics_.is_fully_loaded()) {
    return promise.set_error(404, "Not Found");
  }
  dialog_topics->load_queries_.push_back(std::move(promise));
  if (dialog_topics->load_queries_.size() != 1u) {
    return;
  }

  limit = min(limit, MAX_GET_FORUM_TOPICS);
  auto query_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), dialog_id, generation = dialog_topics->generation_](
                                 Result<td_api::object_ptr<td_api::forumTopics>> result) {
        send_closure(actor_id, &ForumTopicManager::on_load_forum_topics, dialog_id, generation, std::move(result));
      });
  td_->create_handler<GetForumTopicsQuery>(std::move(query_promise))
      ->send(dialog_id, dialog_topics->generation_, string(), dialog_topics->offset_date_,
             dialog_topics->offset_message_id_, dialog_topics->offset_forum_topic_id_, limit);
}

void ForumTopicManager::on_load_forum_topics(DialogId dialog_id, uint32 generation,
                                             Result<td_api::object_ptr<td_api::forumTopics>> &&result) {
  G()->ignore_result_if_closing(result);
  auto *dialog_topics = get_dialog_topics(dialog_id);
  if (dialog_topics == nullptr || dialog_topics->generation_ != generation) {
    return;
  }
  if (result.is_error()) {
    return fail_promises(dialog_topics->load_queries_, result.move_as_error());
  }
  CHECK(result.ok() != nullptr);
  if (result.ok()->topics_.empty()) {
    return fail_promises(dialog_topics->load_queries_, Status::Error(404, "Not Found"));
  }
  set_promises(dialog_topics->load_queries_);
}

vector<ForumTopicId> ForumTopicManager::on_load_forum_topics_page(DialogId dialog_id, DialogTopics *dialog_topics,
                                                                  const td_api::forumTopics *forum_topics) {
  CHECK(dialog_topics != nullptr);
  CHECK(forum_topics != nullptr);
  bool is_first_page = dialog_topics->offset_date_ == 0 && dialog_topics->offset_message_id_ == MessageId() &&
                       dialog_topics->offset_forum_topic_id_ == ForumTopicId();
  vector<ForumTopicId> pinned_forum_topic_ids;
  const Topic *last_topic = nullptr;
  for (auto &forum_topic : forum_topics->topics_) {
    auto forum_topic_id = ForumTopicId(forum_topic->info_->forum_topic_id_);
    auto *topic = get_topic(dialog_topics, forum_topic_id);
    if (topic == nullptr || topic->topic_ == nullptr) {
      continue;
    }
    if (forum_topic->is_pinned_) {
      pinned_forum_topic_ids.push_back(forum_topic_id);
    } else if (topic->private_order_ != 0) {
      // a topic without a known last message date has no place in the list and can't bound the loaded part of it
      last_topic = topic;
    }
  }
  if (is_first_page) {
    set_local_pinned_forum_topics(dialog_id, dialog_topics, std::move(pinned_forum_topic_ids));
  }

  auto &ordered_topics = dialog_topics->ordered_topics_;
  ordered_topics.set_server_total_count(forum_topics->total_count_);
  bool is_last = forum_topics->topics_.empty() ||
                 (forum_topics->next_offset_date_ == dialog_topics->offset_date_ &&
                  MessageId(forum_topics->next_offset_message_id_) == dialog_topics->offset_message_id_ &&
                  ForumTopicId(forum_topics->next_offset_forum_topic_id_) == dialog_topics->offset_forum_topic_id_);
  dialog_topics->offset_date_ = forum_topics->next_offset_date_;
  dialog_topics->offset_message_id_ = MessageId(forum_topics->next_offset_message_id_);
  dialog_topics->offset_forum_topic_id_ = ForumTopicId(forum_topics->next_offset_forum_topic_id_);

  auto last_topic_date = OrderedTopics<ForumTopicId>::max_topic_date();
  if (is_last) {
    // the whole list is loaded
  } else if (last_topic != nullptr) {
    last_topic_date = {last_topic->private_order_, last_topic->info_->get_forum_topic_id()};
  } else {
    last_topic_date = {OrderedTopics<ForumTopicId>::MIN_PINNED_TOPIC_ORDER - 1, ForumTopicId()};
  }
  return ordered_topics.set_last_topic_date(last_topic_date);
}

void ForumTopicManager::get_forum_topics(DialogId dialog_id, string query, int32 offset_date,
                                         MessageId offset_message_id, ForumTopicId offset_forum_topic_id, int32 limit,
                                         Promise<td_api::object_ptr<td_api::forumTopics>> promise) {
  TRY_STATUS_PROMISE(promise, is_forum(dialog_id, true));

  if (offset_date < 0) {
    return promise.set_error(400, "Invalid offset date specified");
  }
  if (offset_message_id != MessageId() && !offset_message_id.is_server()) {
    return promise.set_error(400, "Invalid offset message identifier specified");
  }
  if (offset_forum_topic_id != ForumTopicId()) {
    TRY_STATUS_PROMISE(promise, can_be_forum_topic_id(offset_forum_topic_id));
  }
  if (limit <= 0) {
    return promise.set_error(400, "Invalid limit specified");
  }
  // generations of topic lists start from 1
  td_->create_handler<GetForumTopicsQuery>(std::move(promise))
      ->send(dialog_id, 0, query, offset_date, offset_message_id, offset_forum_topic_id, limit);
}

void ForumTopicManager::on_get_forum_topics(DialogId dialog_id, uint32 generation, bool order_by_creation_date,
                                            MessagesInfo &&info,
                                            vector<telegram_api::object_ptr<telegram_api::ForumTopic>> &&topics,
                                            Promise<td_api::object_ptr<td_api::forumTopics>> &&promise) {
  TRY_STATUS_PROMISE(promise, is_forum(dialog_id, true));
  on_get_topic_messages(dialog_id, std::move(info),
                        transform(topics, [](const auto &topic) { return get_received_forum_topic_id(topic.get()); }),
                        "on_get_forum_topics");
  // a newly pinned topic is pinned above the other ones, so the page is applied from its end
  vector<ForumTopicId> forum_topic_ids;
  for (auto &topic : reversed(topics)) {
    forum_topic_ids.push_back(on_get_forum_topic_impl(dialog_id, std::move(topic), false));
  }
  vector<td_api::object_ptr<td_api::forumTopic>> forum_topics;
  int32 next_offset_date = 0;
  MessageId next_offset_message_id;
  ForumTopicId next_offset_forum_topic_id;
  for (auto forum_topic_id : reversed(forum_topic_ids)) {
    if (!forum_topic_id.is_valid()) {
      continue;
    }
    auto forum_topic_object = get_forum_topic_object(dialog_id, forum_topic_id);
    CHECK(forum_topic_object != nullptr);
    if (order_by_creation_date || forum_topic_object->last_message_ == nullptr) {
      next_offset_date = forum_topic_object->info_->creation_date_;
    } else {
      next_offset_date = forum_topic_object->last_message_->date_;
    }
    next_offset_message_id =
        forum_topic_object->last_message_ != nullptr ? MessageId(forum_topic_object->last_message_->id_) : MessageId();
    next_offset_forum_topic_id = forum_topic_id;
    forum_topics.push_back(std::move(forum_topic_object));
  }

  auto result =
      td_api::make_object<td_api::forumTopics>(info.total_count, std::move(forum_topics), next_offset_date,
                                               next_offset_message_id.get(), next_offset_forum_topic_id.get());

  // a page of the list being loaded extends the loaded part of the list before its topics are sent,
  // so that they are sent once, with their place in the list
  vector<ForumTopicId> changed_forum_topic_ids;
  auto *dialog_topics = get_dialog_topics(dialog_id);
  if (dialog_topics != nullptr && dialog_topics->generation_ == generation) {
    changed_forum_topic_ids = on_load_forum_topics_page(dialog_id, dialog_topics, result.get());
  }
  for (auto forum_topic_id : reversed(forum_topic_ids)) {
    if (forum_topic_id.is_valid()) {
      send_update_forum_topic(dialog_id, get_topic(dialog_id, forum_topic_id));
    }
  }
  for (auto forum_topic_id : changed_forum_topic_ids) {
    if (!td::contains(forum_topic_ids, forum_topic_id)) {
      send_update_forum_topic(dialog_id, get_topic(dialog_id, forum_topic_id));
    }
  }
  promise.set_value(std::move(result));
}

void ForumTopicManager::toggle_forum_topic_is_closed(DialogId dialog_id, ForumTopicId forum_topic_id, bool is_closed,
                                                     Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, is_forum(dialog_id));
  TRY_STATUS_PROMISE(promise, can_be_forum_topic_id(forum_topic_id));
  auto channel_id = dialog_id.get_channel_id();

  if (!td_->chat_manager_->get_channel_permissions(channel_id).can_edit_topics()) {
    auto topic_info = get_topic_info(dialog_id, forum_topic_id);
    if (topic_info != nullptr && !topic_info->is_outgoing()) {
      return promise.set_error(400, "Not enough rights to close or open the topic");
    }
  }

  td_->create_handler<EditForumTopicQuery>(std::move(promise))->send(channel_id, forum_topic_id, is_closed);
}

void ForumTopicManager::toggle_forum_topic_is_hidden(DialogId dialog_id, bool is_hidden, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, is_forum(dialog_id));
  auto channel_id = dialog_id.get_channel_id();

  if (!td_->chat_manager_->get_channel_permissions(channel_id).can_edit_topics()) {
    return promise.set_error(400, "Not enough rights to close or open the topic");
  }

  td_->create_handler<EditForumTopicQuery>(std::move(promise))->send(channel_id, is_hidden);
}

void ForumTopicManager::toggle_forum_topic_is_pinned(DialogId dialog_id, ForumTopicId forum_topic_id, bool is_pinned,
                                                     Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, is_forum(dialog_id, true));
  TRY_STATUS_PROMISE(promise, can_be_forum_topic_id(forum_topic_id));

  if (dialog_id.get_type() == DialogType::Channel) {
    auto channel_id = dialog_id.get_channel_id();
    if (!td_->chat_manager_->get_channel_permissions(channel_id).can_pin_topics()) {
      return promise.set_error(400, "Not enough rights to pin or unpin the topic");
    }
  }

  td_->create_handler<UpdatePinnedForumTopicQuery>(std::move(promise))->send(dialog_id, forum_topic_id, is_pinned);
}

void ForumTopicManager::set_pinned_forum_topics(DialogId dialog_id, vector<ForumTopicId> forum_topic_ids,
                                                Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, is_forum(dialog_id, true));
  for (auto forum_topic_id : forum_topic_ids) {
    TRY_STATUS_PROMISE(promise, can_be_forum_topic_id(forum_topic_id));
  }

  if (dialog_id.get_type() == DialogType::Channel) {
    auto channel_id = dialog_id.get_channel_id();
    if (!td_->chat_manager_->get_channel_permissions(channel_id).can_pin_topics()) {
      return promise.set_error(400, "Not enough rights to reorder forum topics");
    }
  }

  td_->create_handler<ReorderPinnedForumTopicsQuery>(std::move(promise))->send(dialog_id, forum_topic_ids);
}

void ForumTopicManager::delete_forum_topic(DialogId dialog_id, ForumTopicId forum_topic_id, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, is_forum(dialog_id, true));
  TRY_STATUS_PROMISE(promise, can_be_forum_topic_id(forum_topic_id));
  if (forum_topic_id == ForumTopicId::general()) {
    return promise.set_error(400, "The General topic can't be deleted");
  }

  if (dialog_id.get_type() == DialogType::Channel) {
    auto channel_id = dialog_id.get_channel_id();
    if (!td_->chat_manager_->get_channel_permissions(channel_id).can_delete_messages()) {
      auto topic_info = get_topic_info(dialog_id, forum_topic_id);
      if (topic_info != nullptr && !topic_info->is_outgoing()) {
        return promise.set_error(400, "Not enough rights to delete the topic");
      }
    }
  }

  auto delete_promise = PromiseCreator::lambda([actor_id = actor_id(this), dialog_id, forum_topic_id,
                                                promise = std::move(promise)](Result<Unit> result) mutable {
    if (result.is_error()) {
      return promise.set_error(result.move_as_error());
    }
    send_closure(actor_id, &ForumTopicManager::on_delete_forum_topic, dialog_id, forum_topic_id, std::move(promise));
  });
  td_->message_query_manager_->delete_topic_history(dialog_id, forum_topic_id, std::move(delete_promise));
}

void ForumTopicManager::on_delete_forum_topic(DialogId dialog_id, ForumTopicId forum_topic_id,
                                              Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  auto *dialog_topics = dialog_topics_.get_pointer(dialog_id);
  if (dialog_topics != nullptr) {
    CHECK(forum_topic_id.is_valid());
    auto *topic = get_topic(dialog_topics, forum_topic_id);
    if (topic != nullptr) {
      dialog_topics->ordered_topics_.update_order(forum_topic_id, topic->private_order_, 0);
      if (topic->pinned_order_ != 0) {
        dialog_topics->ordered_topics_.unpin_topic(forum_topic_id);
      }
      if (topic->info_ != nullptr && !td_->auth_manager_->is_bot()) {
        send_update_forum_topic_deleted(dialog_id, forum_topic_id);
      }
    }
    dialog_topics->topics_.erase(forum_topic_id);
    dialog_topics->deleted_topic_ids_.insert(forum_topic_id);
  }
  delete_topic_from_database(dialog_id, forum_topic_id, std::move(promise));
}

void ForumTopicManager::delete_all_dialog_topics(DialogId dialog_id) {
  dialog_topics_.erase(dialog_id);

  auto message_thread_db = G()->td_db()->get_message_thread_db_async();
  if (message_thread_db == nullptr) {
    return;
  }

  LOG(INFO) << "Delete all topics in " << dialog_id << " from database";
  message_thread_db->delete_all_dialog_message_threads(dialog_id, Auto());
}

void ForumTopicManager::on_dialog_is_forum_changed(DialogId dialog_id) {
  // messages are assigned to topics depending on whether the chat is a forum, so the topics must be received again
  auto *dialog_topics = get_dialog_topics(dialog_id);
  if (dialog_topics == nullptr) {
    return;
  }
  vector<ForumTopicId> forum_topic_ids;
  if (!td_->auth_manager_->is_bot()) {
    dialog_topics->topics_.foreach([&](const ForumTopicId &forum_topic_id, unique_ptr<Topic> &topic) {
      if (topic->info_ != nullptr) {
        forum_topic_ids.push_back(forum_topic_id);
      }
    });
  }
  auto load_queries = std::move(dialog_topics->load_queries_);
  dialog_topics_.erase(dialog_id);
  // the app must forget the topics too, without having to track whether the chat is a forum
  for (auto forum_topic_id : forum_topic_ids) {
    send_update_forum_topic_deleted(dialog_id, forum_topic_id);
  }
  fail_promises(load_queries, Status::Error(400, "The chat is not a forum"));
}

void ForumTopicManager::on_forum_topic_edited(DialogId dialog_id, ForumTopicId forum_topic_id,
                                              const ForumTopicEditedData &edited_data) {
  auto topic = get_topic(dialog_id, forum_topic_id);
  if (topic == nullptr || topic->info_ == nullptr) {
    return;
  }
  if (topic->info_->apply_edited_data(edited_data)) {
    send_update_forum_topic_info(topic->info_.get());
    topic->need_save_to_database_ = true;
  }
  save_topic_to_database(dialog_id, topic);
}

bool ForumTopicManager::on_get_forum_topic_error(DialogId dialog_id, ForumTopicId forum_topic_id, const Status &status,
                                                 const char *source) {
  return td_->dialog_manager_->on_get_dialog_error(dialog_id, status, source);
}

void ForumTopicManager::on_get_forum_topic_info(DialogId dialog_id, const ForumTopicInfo &topic_info,
                                                const char *source) {
  if (!can_be_forum(dialog_id)) {
    LOG(ERROR) << "Receive forum topics in " << dialog_id << " from " << source;
    return;
  }

  auto *dialog_topics = add_dialog_topics(dialog_id);
  CHECK(dialog_topics != nullptr);
  auto forum_topic_info = td::make_unique<ForumTopicInfo>(topic_info);
  ForumTopicId forum_topic_id = forum_topic_info->get_forum_topic_id();
  CHECK(can_be_forum_topic_id(forum_topic_id).is_ok());
  auto topic = add_topic(dialog_topics, forum_topic_id);
  if (topic == nullptr) {
    return;
  }
  set_topic_info(dialog_id, topic, std::move(forum_topic_info));
  save_topic_to_database(dialog_id, topic);
}

void ForumTopicManager::on_get_forum_topic_infos(DialogId dialog_id,
                                                 vector<tl_object_ptr<telegram_api::ForumTopic>> &&forum_topics,
                                                 const char *source) {
  if (forum_topics.empty()) {
    return;
  }
  if (dialog_id == DialogId()) {
    for (auto &forum_topic : forum_topics) {
      auto forum_topic_info = td::make_unique<ForumTopicInfo>(td_, forum_topic, DialogId());
      ForumTopicId forum_topic_id = forum_topic_info->get_forum_topic_id();
      if (can_be_forum_topic_id(forum_topic_id).is_error()) {
        continue;
      }
      dialog_id = forum_topic_info->get_dialog_id();
      if (!can_be_forum(dialog_id)) {
        LOG(ERROR) << "Receive forum topics in " << dialog_id << " from " << source;
        return;
      }
      auto *dialog_topics = add_dialog_topics(dialog_id);
      CHECK(dialog_topics != nullptr);
      auto topic = add_topic(dialog_topics, forum_topic_id);
      if (topic != nullptr) {
        set_topic_info(dialog_id, topic, std::move(forum_topic_info));
        save_topic_to_database(dialog_id, topic);
      }
    }
    return;
  }
  if (!can_be_forum(dialog_id)) {
    LOG(ERROR) << "Receive forum topics in " << dialog_id << " from " << source;
    return;
  }

  auto *dialog_topics = add_dialog_topics(dialog_id);
  CHECK(dialog_topics != nullptr);
  for (auto &forum_topic : forum_topics) {
    auto forum_topic_info = td::make_unique<ForumTopicInfo>(td_, forum_topic, dialog_id);
    ForumTopicId forum_topic_id = forum_topic_info->get_forum_topic_id();
    if (can_be_forum_topic_id(forum_topic_id).is_error()) {
      continue;
    }
    auto topic = add_topic(dialog_topics, forum_topic_id);
    if (topic != nullptr) {
      set_topic_info(dialog_id, topic, std::move(forum_topic_info));
      save_topic_to_database(dialog_id, topic);
    }
  }
}

ForumTopicId ForumTopicManager::get_received_forum_topic_id(const telegram_api::ForumTopic *forum_topic) {
  CHECK(forum_topic != nullptr);
  switch (forum_topic->get_id()) {
    case telegram_api::forumTopicDeleted::ID:
      return ForumTopicId(static_cast<const telegram_api::forumTopicDeleted *>(forum_topic)->id_);
    case telegram_api::forumTopic::ID:
      return ForumTopicId(static_cast<const telegram_api::forumTopic *>(forum_topic)->id_);
    default:
      UNREACHABLE();
      return ForumTopicId();
  }
}

void ForumTopicManager::on_get_topic_messages(DialogId dialog_id, MessagesInfo &&info,
                                              vector<ForumTopicId> received_topic_ids, const char *source) {
  // the topics of the messages are received together with them, so they must not be loaded once more
  add_dialog_topics(dialog_id)->received_topic_ids_ = std::move(received_topic_ids);
  td_->messages_manager_->on_get_messages(dialog_id, std::move(info.messages), false, Promise<Unit>(), source);
  auto *dialog_topics = get_dialog_topics(dialog_id);
  if (dialog_topics != nullptr) {
    dialog_topics->received_topic_ids_.clear();
  }
}

ForumTopicId ForumTopicManager::on_get_forum_topic_impl(DialogId dialog_id,
                                                        tl_object_ptr<telegram_api::ForumTopic> &&forum_topic,
                                                        bool need_update) {
  CHECK(forum_topic != nullptr);
  switch (forum_topic->get_id()) {
    case telegram_api::forumTopicDeleted::ID: {
      auto forum_topic_id = ForumTopicId(static_cast<const telegram_api::forumTopicDeleted *>(forum_topic.get())->id_);
      if (!forum_topic_id.is_valid()) {
        LOG(ERROR) << "Receive " << to_string(forum_topic);
        return ForumTopicId();
      }
      if (forum_topic_id != ForumTopicId::general()) {
        on_delete_forum_topic(dialog_id, forum_topic_id, Promise<Unit>());
      }
      return ForumTopicId();
    }
    case telegram_api::forumTopic::ID: {
      auto forum_topic_info = td::make_unique<ForumTopicInfo>(td_, forum_topic, dialog_id);
      auto forum_topic_id = forum_topic_info->get_forum_topic_id();
      Topic *topic = add_topic(dialog_id, forum_topic_id);
      if (topic == nullptr) {
        return ForumTopicId();
      }
      auto current_notification_settings =
          topic->topic_ == nullptr ? nullptr : topic->topic_->get_notification_settings();
      auto forum_topic_full = td::make_unique<ForumTopic>(td_, std::move(forum_topic), current_notification_settings);
      if (forum_topic_full->is_short() && !td_->auth_manager_->is_bot()) {
        LOG(ERROR) << "Receive short forum topic";
        return ForumTopicId();
      }
      auto old_last_message_id = topic->topic_ == nullptr ? MessageId() : topic->topic_->get_last_message_id();
      auto old_last_message_date = topic->last_message_date_;
      auto old_last_read_inbox_message_id =
          topic->topic_ == nullptr ? MessageId() : topic->topic_->get_last_read_inbox_message_id();
      auto old_unread_count = topic->topic_ == nullptr ? 0 : topic->topic_->get_unread_count();
      if (topic->topic_ == nullptr || true) {
        auto old_file_ids = get_topic_file_ids(topic);
        topic->topic_ = std::move(forum_topic_full);
        topic->need_save_to_database_ = true;  // TODO temporary
        td_->draft_message_manager_->change_draft_message_files(
            dialog_id, MessageTopic::forum(dialog_id, forum_topic_id), old_file_ids, get_topic_file_ids(topic), true);
      }
      topic->receive_date_ = G()->unix_time();
      set_topic_info(dialog_id, topic, std::move(forum_topic_info));
      auto last_message_id = topic->topic_->get_last_message_id();
      if (old_last_message_id > last_message_id) {
        // the topic was requested before its newer last message was received, so its unread count is older too
        topic->topic_->set_last_message_id(old_last_message_id);
        topic->last_message_date_ = old_last_message_date;
        if (old_last_read_inbox_message_id >= topic->topic_->get_last_read_inbox_message_id()) {
          topic->topic_->update_last_read_inbox_message_id(old_last_read_inbox_message_id, old_unread_count);
          topic->topic_->set_unread_count(old_unread_count);
        }
      } else if (last_message_id.is_valid()) {
        topic->last_message_date_ = td_->messages_manager_->get_get_message_date(dialog_id)(last_message_id);
      }
      update_topic_order(dialog_id, topic);
      if (need_update) {
        send_update_forum_topic(dialog_id, topic);
      }
      save_topic_to_database(dialog_id, topic);
      return forum_topic_id;
    }
    default:
      UNREACHABLE();
      return ForumTopicId();
  }
}

vector<FileId> ForumTopicManager::get_topic_file_ids(const Topic *topic) const {
  if (topic == nullptr || topic->topic_ == nullptr) {
    return {};
  }
  return get_draft_message_file_ids(td_, topic->topic_->get_draft_message());
}

int32 ForumTopicManager::get_forum_topic_id_object(DialogId dialog_id, ForumTopicId forum_topic_id) {
  if (forum_topic_id == ForumTopicId()) {
    return 0;
  }

  // TODO load topic from database or get from the server

  return forum_topic_id.get();
}

td_api::object_ptr<td_api::forumTopic> ForumTopicManager::get_forum_topic_object(DialogId dialog_id,
                                                                                 ForumTopicId forum_topic_id) const {
  auto topic = get_topic(dialog_id, forum_topic_id);
  if (topic == nullptr || topic->topic_ == nullptr) {
    return nullptr;
  }
  CHECK(topic->info_ != nullptr);
  return topic->topic_->get_forum_topic_object(td_, dialog_id, *topic->info_, get_topic_public_order(dialog_id, topic));
}

int64 ForumTopicManager::get_topic_public_order(DialogId dialog_id, const Topic *topic) const {
  CHECK(topic != nullptr);
  if (td_->auth_manager_->is_bot()) {
    return topic->topic_ == nullptr ? 0 : topic->topic_->get_order(topic->last_message_date_);
  }
  if (topic->info_ == nullptr) {
    return 0;
  }
  auto *dialog_topics = dialog_topics_.get_pointer(dialog_id);
  CHECK(dialog_topics != nullptr);
  return dialog_topics->ordered_topics_.get_public_order(topic->private_order_, topic->info_->get_forum_topic_id());
}

void ForumTopicManager::set_local_pinned_forum_topics(DialogId dialog_id, DialogTopics *dialog_topics,
                                                      vector<ForumTopicId> forum_topic_ids) {
  CHECK(dialog_topics != nullptr);
  td::remove_if(forum_topic_ids, [&](ForumTopicId forum_topic_id) {
    auto *topic = get_topic(dialog_topics, forum_topic_id);
    return topic == nullptr || topic->topic_ == nullptr;
  });
  auto &ordered_topics = dialog_topics->ordered_topics_;
  if (ordered_topics.get_pinned_topic_ids() == forum_topic_ids) {
    return;
  }
  vector<ForumTopicId> added_forum_topic_ids;
  vector<ForumTopicId> removed_forum_topic_ids;
  ordered_topics.get_pinned_topic_changes(std::move(forum_topic_ids), added_forum_topic_ids, removed_forum_topic_ids);
  for (auto forum_topic_id : removed_forum_topic_ids) {
    ordered_topics.unpin_topic(forum_topic_id);
    auto *topic = get_topic(dialog_topics, forum_topic_id);
    if (topic == nullptr) {
      continue;
    }
    topic->pinned_order_ = 0;
    if (topic->topic_ != nullptr) {
      topic->topic_->set_is_pinned(false);
    }
    on_forum_topic_changed(dialog_id, topic);
  }
  for (auto forum_topic_id : added_forum_topic_ids) {
    auto *topic = get_topic(dialog_topics, forum_topic_id);
    CHECK(topic != nullptr && topic->topic_ != nullptr);
    auto pinned_order = ordered_topics.pin_topic(forum_topic_id);
    if (pinned_order != 0) {
      topic->pinned_order_ = pinned_order;
    }
    topic->topic_->set_is_pinned(true);
    on_forum_topic_changed(dialog_id, topic);
  }
}

Status ForumTopicManager::is_forum(DialogId dialog_id, bool allow_bots) {
  if (!td_->dialog_manager_->have_dialog_force(dialog_id, "ForumTopicManager::is_forum")) {
    return Status::Error(400, "Chat not found");
  }
  switch (dialog_id.get_type()) {
    case DialogType::User: {
      auto bot_user_id = td_->auth_manager_->is_bot() ? td_->user_manager_->get_my_id() : dialog_id.get_user_id();
      if (td_->user_manager_->is_user_forum_bot(bot_user_id)) {
        if (!allow_bots) {
          return Status::Error(400, "The chat is not a supergroup forum");
        }
        return Status::OK();
      }
      break;
    }
    case DialogType::Channel:
      if (td_->chat_manager_->is_forum_channel(dialog_id.get_channel_id())) {
        return Status::OK();
      }
      break;
    case DialogType::Chat:
    case DialogType::SecretChat:
    case DialogType::None:
    default:
      break;
  }
  return Status::Error(400, "The chat is not a forum");
}

bool ForumTopicManager::can_be_forum(DialogId dialog_id) const {
  switch (dialog_id.get_type()) {
    case DialogType::User:
      return td_->auth_manager_->is_bot() || td_->user_manager_->is_user_bot(dialog_id.get_user_id());
    case DialogType::Channel:
      return !td_->chat_manager_->is_broadcast_channel(dialog_id.get_channel_id()) &&
             !td_->chat_manager_->is_monoforum_channel(dialog_id.get_channel_id());
    case DialogType::Chat:
    case DialogType::SecretChat:
    case DialogType::None:
    default:
      break;
  }
  return false;
}

Status ForumTopicManager::can_be_forum_topic_id(ForumTopicId forum_topic_id) {
  if (!forum_topic_id.is_valid()) {
    return Status::Error(400, "Invalid forum topic identifier specified");
  }
  return Status::OK();
}

bool ForumTopicManager::can_send_message_to_forum_topic(DialogId dialog_id, ForumTopicId forum_topic_id) const {
  auto *topic_info = get_topic_info(dialog_id, forum_topic_id);
  if (topic_info == nullptr) {
    return true;  // allow sending to unknown topic
  }
  if (topic_info->is_closed() && !topic_info->is_outgoing() && dialog_id.get_type() == DialogType::Channel &&
      !td_->chat_manager_->get_channel_status(dialog_id.get_channel_id()).can_edit_topics()) {
    return false;  // don't allow sending to closed topic
  }
  return true;
}

ForumTopicManager::DialogTopics *ForumTopicManager::add_dialog_topics(DialogId dialog_id) {
  auto *dialog_topics = dialog_topics_.get_pointer(dialog_id);
  if (dialog_topics == nullptr) {
    auto new_dialog_topics = make_unique<DialogTopics>();
    new_dialog_topics->generation_ = ++current_topic_list_generation_;
    dialog_topics = new_dialog_topics.get();
    dialog_topics_.set(dialog_id, std::move(new_dialog_topics));
  }
  return dialog_topics;
}

ForumTopicManager::DialogTopics *ForumTopicManager::get_dialog_topics(DialogId dialog_id) {
  return dialog_topics_.get_pointer(dialog_id);
}

ForumTopicManager::Topic *ForumTopicManager::add_topic(DialogTopics *dialog_topics, ForumTopicId forum_topic_id) {
  auto topic = dialog_topics->topics_.get_pointer(forum_topic_id);
  if (topic == nullptr) {
    if (dialog_topics->deleted_topic_ids_.count(forum_topic_id) > 0) {
      return nullptr;
    }
    auto new_topic = make_unique<Topic>();
    topic = new_topic.get();
    CHECK(forum_topic_id.is_valid());
    dialog_topics->topics_.set(forum_topic_id, std::move(new_topic));
  }
  return topic;
}

ForumTopicManager::Topic *ForumTopicManager::get_topic(DialogTopics *dialog_topics, ForumTopicId forum_topic_id) {
  return dialog_topics->topics_.get_pointer(forum_topic_id);
}

ForumTopicManager::Topic *ForumTopicManager::add_topic(DialogId dialog_id, ForumTopicId forum_topic_id) {
  return add_topic(add_dialog_topics(dialog_id), forum_topic_id);
}

ForumTopicManager::Topic *ForumTopicManager::get_topic(DialogId dialog_id, ForumTopicId forum_topic_id) {
  auto *dialog_topics = dialog_topics_.get_pointer(dialog_id);
  if (dialog_topics == nullptr) {
    return nullptr;
  }
  return dialog_topics->topics_.get_pointer(forum_topic_id);
}

const ForumTopicManager::Topic *ForumTopicManager::get_topic(DialogId dialog_id, ForumTopicId forum_topic_id) const {
  auto *dialog_topics = dialog_topics_.get_pointer(dialog_id);
  if (dialog_topics == nullptr) {
    return nullptr;
  }
  return dialog_topics->topics_.get_pointer(forum_topic_id);
}

ForumTopicInfo *ForumTopicManager::get_topic_info(DialogId dialog_id, ForumTopicId forum_topic_id) {
  auto *topic = get_topic(dialog_id, forum_topic_id);
  if (topic == nullptr) {
    return nullptr;
  }
  return topic->info_.get();
}

const ForumTopicInfo *ForumTopicManager::get_topic_info(DialogId dialog_id, ForumTopicId forum_topic_id) const {
  auto *topic = get_topic(dialog_id, forum_topic_id);
  if (topic == nullptr) {
    return nullptr;
  }
  return topic->info_.get();
}

void ForumTopicManager::set_topic_info(DialogId dialog_id, Topic *topic, unique_ptr<ForumTopicInfo> forum_topic_info) {
  if (topic->info_ == nullptr || *topic->info_ != *forum_topic_info) {
    topic->info_ = std::move(forum_topic_info);
    send_update_forum_topic_info(topic->info_.get());
    topic->need_save_to_database_ = true;
  }
}

td_api::object_ptr<td_api::updateForumTopicInfo> ForumTopicManager::get_update_forum_topic_info_object(
    const ForumTopicInfo *topic_info) const {
  return td_api::make_object<td_api::updateForumTopicInfo>(topic_info->get_forum_topic_info_object(td_));
}

void ForumTopicManager::send_update_forum_topic_info(const ForumTopicInfo *topic_info) const {
  if (td_->auth_manager_->is_bot()) {
    return;
  }
  send_closure(G()->td(), &Td::send_update, get_update_forum_topic_info_object(topic_info));
}

void ForumTopicManager::send_update_forum_topic_deleted(DialogId dialog_id, ForumTopicId forum_topic_id) const {
  send_closure(
      G()->td(), &Td::send_update,
      td_api::make_object<td_api::updateForumTopicDeleted>(
          td_->dialog_manager_->get_chat_id_object(dialog_id, "updateForumTopicDeleted"), forum_topic_id.get()));
}

td_api::object_ptr<td_api::updateForumTopic> ForumTopicManager::get_update_forum_topic_object(
    DialogId dialog_id, const Topic *topic) const {
  CHECK(topic != nullptr);
  CHECK(topic->info_ != nullptr && topic->topic_ != nullptr);
  return td_api::make_object<td_api::updateForumTopic>(
      topic->topic_->get_forum_topic_object(td_, dialog_id, *topic->info_, get_topic_public_order(dialog_id, topic)));
}

void ForumTopicManager::send_update_forum_topic(DialogId dialog_id, const Topic *topic) const {
  if (td_->auth_manager_->is_bot() || topic->info_ == nullptr || topic->topic_ == nullptr) {
    return;
  }
  send_closure(G()->td(), &Td::send_update, get_update_forum_topic_object(dialog_id, topic));
}

void ForumTopicManager::on_forum_topic_changed(DialogId dialog_id, Topic *topic) {
  CHECK(topic != nullptr);
  update_topic_order(dialog_id, topic);
  send_update_forum_topic(dialog_id, topic);
  topic->need_save_to_database_ = true;
  save_topic_to_database(dialog_id, topic);
}

void ForumTopicManager::update_topic_order(DialogId dialog_id, Topic *topic) {
  CHECK(topic != nullptr);
  if (td_->auth_manager_->is_bot() || topic->info_ == nullptr) {
    return;
  }
  auto *dialog_topics = get_dialog_topics(dialog_id);
  CHECK(dialog_topics != nullptr);
  auto &ordered_topics = dialog_topics->ordered_topics_;
  auto forum_topic_id = topic->info_->get_forum_topic_id();

  int64 new_order = 0;
  if (topic->topic_ != nullptr) {
    if (topic->topic_->is_pinned()) {
      if (topic->pinned_order_ == 0) {
        topic->pinned_order_ = ordered_topics.pin_topic(forum_topic_id);
      }
    } else if (topic->pinned_order_ != 0) {
      ordered_topics.unpin_topic(forum_topic_id);
      topic->pinned_order_ = 0;
    }
    new_order = topic->pinned_order_ != 0 ? topic->pinned_order_ : topic->topic_->get_order(topic->last_message_date_);
  }
  ordered_topics.update_order(forum_topic_id, topic->private_order_, new_order);
  topic->private_order_ = new_order;
}

void ForumTopicManager::save_topic_to_database(DialogId dialog_id, const Topic *topic) {
  CHECK(topic != nullptr);
  if (topic->info_ == nullptr || !topic->need_save_to_database_) {
    return;
  }
  topic->need_save_to_database_ = false;

  auto message_thread_db = G()->td_db()->get_message_thread_db_async();
  if (message_thread_db == nullptr) {
    return;
  }

  auto forum_topic_id = topic->info_->get_forum_topic_id();
  LOG(INFO) << "Save topic of " << forum_topic_id << " in " << dialog_id << " to database";
  message_thread_db->add_message_thread(dialog_id, forum_topic_id, 0, log_event_store(*topic), Auto());
}

void ForumTopicManager::delete_topic_from_database(DialogId dialog_id, ForumTopicId forum_topic_id,
                                                   Promise<Unit> &&promise) {
  auto message_thread_db = G()->td_db()->get_message_thread_db_async();
  if (message_thread_db == nullptr) {
    return promise.set_value(Unit());
  }

  LOG(INFO) << "Delete topic of " << forum_topic_id << " in " << dialog_id << " from database";
  message_thread_db->delete_message_thread(dialog_id, forum_topic_id, std::move(promise));
}

void ForumTopicManager::on_topic_message_count_changed(DialogId dialog_id, ForumTopicId forum_topic_id, int diff) {
  if (!can_be_forum(dialog_id) || can_be_forum_topic_id(forum_topic_id).is_error()) {
    LOG(ERROR) << "Change by " << diff << " number of loaded messages in thread of " << forum_topic_id << " in "
               << dialog_id;
    return;
  }

  LOG(INFO) << "Change by " << diff << " number of loaded messages in thread of " << forum_topic_id << " in "
            << dialog_id;
  auto *dialog_topics = add_dialog_topics(dialog_id);
  auto topic = add_topic(dialog_topics, forum_topic_id);
  if (topic == nullptr) {
    return;
  }
  topic->message_count_ += diff;
  CHECK(topic->message_count_ >= 0);
}

void ForumTopicManager::reload_topic(DialogId dialog_id, DialogTopics *dialog_topics, ForumTopicId forum_topic_id) {
  CHECK(dialog_topics != nullptr);
  // messages of a loaded topic are received before the topic itself
  if (td::contains(dialog_topics->reload_topic_ids_, forum_topic_id) ||
      td::contains(dialog_topics->reloading_topic_ids_, forum_topic_id)) {
    return;
  }
  auto *topic = get_topic(dialog_topics, forum_topic_id);
  if (topic != nullptr && topic->topic_ == nullptr && topic->is_not_received_) {
    return;
  }
  LOG(INFO) << "Reload " << forum_topic_id << " in " << dialog_id;
  dialog_topics->reload_topic_ids_.push_back(forum_topic_id);
  if (dialog_topics->reloading_topic_ids_.empty()) {
    reload_topics(dialog_id, dialog_topics);
  }
}

void ForumTopicManager::reload_topics(DialogId dialog_id, DialogTopics *dialog_topics) {
  CHECK(dialog_topics != nullptr);
  CHECK(dialog_topics->reloading_topic_ids_.empty());
  auto &reload_topic_ids = dialog_topics->reload_topic_ids_;
  if (reload_topic_ids.empty() || reload_topics_timeout_.has_timeout(dialog_id.get())) {
    // new topics wait for the retry of a failed reload
    return;
  }
  if (is_forum(dialog_id, true).is_error()) {
    reload_topic_ids.clear();
    return;
  }
  auto size = min(reload_topic_ids.size(), MAX_GET_FORUM_TOPICS_BY_ID);
  vector<ForumTopicId> forum_topic_ids(reload_topic_ids.begin(), reload_topic_ids.begin() + size);
  reload_topic_ids.erase(reload_topic_ids.begin(), reload_topic_ids.begin() + size);

  dialog_topics->reloading_topic_ids_ = forum_topic_ids;
  auto query_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), dialog_id, generation = dialog_topics->generation_](Result<Unit> &&result) {
        send_closure(actor_id, &ForumTopicManager::on_reload_topics, dialog_id, generation, std::move(result));
      });
  td_->create_handler<GetForumTopicsByIdQuery>(std::move(query_promise))->send(dialog_id, forum_topic_ids);
}

void ForumTopicManager::on_reload_topics(DialogId dialog_id, uint32 generation, Result<Unit> &&result) {
  if (G()->close_flag()) {
    return;
  }
  auto *dialog_topics = get_dialog_topics(dialog_id);
  if (dialog_topics == nullptr || dialog_topics->generation_ != generation) {
    return;
  }
  if (result.is_error()) {
    LOG(INFO) << "Failed to reload topics in " << dialog_id << ": " << result.error();
    if (is_forum(dialog_id, true).is_ok() &&
        td_->dialog_manager_->have_input_peer(dialog_id, false, AccessRights::Read)) {
      // the topics would stay unknown, so they are requested again later, as a failed getChannelDifference is
      append(dialog_topics->reload_topic_ids_, std::move(dialog_topics->reloading_topic_ids_));
      dialog_topics->reloading_topic_ids_.clear();
      auto &delay = dialog_topics->reload_topics_retry_delay_;
      if (delay == 0) {
        delay = 1;
      }
      reload_topics_timeout_.set_timeout_in(dialog_id.get(), Random::fast(delay * 800, delay * 1200) * 1e-3);
      delay *= 2;
      if (delay > 60) {
        delay = Random::fast(60, 80);
      }
      return;
    }
  } else {
    dialog_topics->reload_topics_retry_delay_ = 0;
  }
  for (auto forum_topic_id : dialog_topics->reloading_topic_ids_) {
    auto *topic = get_topic(dialog_topics, forum_topic_id);
    if (topic != nullptr && topic->topic_ == nullptr) {
      // the topic is requested again only after it is removed from memory
      topic->is_not_received_ = true;
    }
  }
  dialog_topics->reloading_topic_ids_.clear();
  reload_topics(dialog_id, dialog_topics);
}

void ForumTopicManager::on_reload_topics_timeout_callback(void *forum_topic_manager_ptr, int64 dialog_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto forum_topic_manager = static_cast<ForumTopicManager *>(forum_topic_manager_ptr);
  send_closure_later(forum_topic_manager->actor_id(forum_topic_manager), &ForumTopicManager::on_reload_topics_timeout,
                     DialogId(dialog_id_int));
}

void ForumTopicManager::on_reload_topics_timeout(DialogId dialog_id) {
  auto *dialog_topics = get_dialog_topics(dialog_id);
  if (dialog_topics == nullptr || !dialog_topics->reloading_topic_ids_.empty()) {
    return;
  }
  reload_topics(dialog_id, dialog_topics);
}

void ForumTopicManager::reload_topic_without_last_message(DialogId dialog_id, ForumTopicId forum_topic_id) {
  if (G()->close_flag()) {
    return;
  }
  auto *dialog_topics = get_dialog_topics(dialog_id);
  if (dialog_topics == nullptr) {
    return;
  }
  auto *topic = get_topic(dialog_topics, forum_topic_id);
  if (topic == nullptr || topic->topic_ == nullptr || topic->topic_->get_last_message_id().is_valid()) {
    return;
  }
  reload_topic(dialog_id, dialog_topics, forum_topic_id);
}

bool ForumTopicManager::is_last_topic_message(DialogId dialog_id, ForumTopicId forum_topic_id,
                                              MessageId message_id) const {
  auto *topic = get_topic(dialog_id, forum_topic_id);
  return topic != nullptr && topic->topic_ != nullptr && topic->topic_->get_last_message_id() == message_id;
}

void ForumTopicManager::on_topic_message_added(DialogId dialog_id, ForumTopicId forum_topic_id, MessageId message_id,
                                               int32 message_date, bool from_update, bool need_update, bool is_new,
                                               const char *source) {
  if (td_->auth_manager_->is_bot() || !can_be_forum(dialog_id) || !forum_topic_id.is_valid()) {
    return;
  }
  CHECK(message_id.is_valid());

  auto *topic = add_topic(dialog_id, forum_topic_id);
  if (topic == nullptr) {
    return;
  }
  auto last_message_id = topic->topic_ == nullptr ? MessageId() : topic->topic_->get_last_message_id();
  topic->ordered_messages_.insert(message_id, from_update, last_message_id, source);
  if (topic->topic_ == nullptr) {
    auto *dialog_topics = get_dialog_topics(dialog_id);
    if (td::contains(dialog_topics->received_topic_ids_, forum_topic_id)) {
      return;
    }
    return reload_topic(dialog_id, dialog_topics, forum_topic_id);
  }
  if (message_id <= last_message_id) {
    return;
  }

  LOG(INFO) << "Add " << message_id << " to " << forum_topic_id << " of " << dialog_id
            << " with from_update = " << from_update << ", need_update = " << need_update << " and is_new = " << is_new;
  if (from_update && is_new) {
    topic->topic_->set_last_message_id(message_id);
    topic->last_message_date_ = message_date;
    // the server counts unread messages in a topic only after it was read for the first time
    auto last_read_inbox_message_id = topic->topic_->get_last_read_inbox_message_id();
    if (need_update && last_read_inbox_message_id.is_valid() && message_id > last_read_inbox_message_id &&
        td_->messages_manager_->get_is_counted_as_unread(dialog_id, MessageType::Server)(message_id)) {
      topic->topic_->set_unread_count(topic->topic_->get_unread_count() + 1);
    }
    on_forum_topic_changed(dialog_id, topic);
  } else {
    // a message newer than the last known one was received not from an update, so the last message is unknown
    reload_topic(dialog_id, get_dialog_topics(dialog_id), forum_topic_id);
  }
}

void ForumTopicManager::on_topic_message_updated(DialogId dialog_id, ForumTopicId forum_topic_id,
                                                 MessageId message_id) {
  if (td_->auth_manager_->is_bot() || message_id.is_scheduled()) {
    return;
  }
  auto *topic = get_topic(dialog_id, forum_topic_id);
  if (topic == nullptr || topic->topic_ == nullptr || topic->topic_->get_last_message_id() != message_id) {
    return;
  }
  send_update_forum_topic(dialog_id, topic);
}

void ForumTopicManager::on_topic_message_deleted(DialogId dialog_id, ForumTopicId forum_topic_id, MessageId message_id,
                                                 bool only_from_memory, const char *source) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }
  auto *topic = get_topic(dialog_id, forum_topic_id);
  // the topic list can be dropped and the chat can stop being a forum while its messages are still in memory
  if (topic == nullptr || !topic->ordered_messages_.has_message(message_id)) {
    return;
  }

  LOG(INFO) << "Delete " << message_id << " from " << forum_topic_id << " of " << dialog_id << " from " << source;
  if (!only_from_memory && topic->topic_ != nullptr && topic->topic_->get_last_message_id() == message_id) {
    MessageId new_last_message_id;
    auto it = topic->ordered_messages_.get_const_iterator(message_id);
    CHECK(*it != nullptr);
    --it;
    if (*it != nullptr) {
      new_last_message_id = (*it)->get_message_id();
      topic->last_message_date_ = td_->messages_manager_->get_get_message_date(dialog_id)(new_last_message_id);
      topic->topic_->set_last_message_id(new_last_message_id);
      on_forum_topic_changed(dialog_id, topic);
    } else {
      // the previous message is unknown, so the topic keeps the order until it gets another last message;
      // a sent message is added again right away with its new identifier, so the topic is received again only
      // if it has no last message after that
      topic->topic_->set_last_message_id(MessageId());
      send_closure_later(actor_id(this), &ForumTopicManager::reload_topic_without_last_message, dialog_id,
                         forum_topic_id);
    }
  }
  if (!only_from_memory && topic->topic_ != nullptr) {
    auto last_read_inbox_message_id = topic->topic_->get_last_read_inbox_message_id();
    if (last_read_inbox_message_id.is_valid() && message_id > last_read_inbox_message_id &&
        td_->messages_manager_->get_is_counted_as_unread(dialog_id, MessageType::Server)(message_id) &&
        topic->topic_->set_unread_count(topic->topic_->get_unread_count() - 1)) {
      on_forum_topic_changed(dialog_id, topic);
    }
  }
  topic->ordered_messages_.erase(message_id, only_from_memory, source);
}

void ForumTopicManager::on_topic_mention_count_changed(DialogId dialog_id, ForumTopicId forum_topic_id, int32 count,
                                                       bool is_relative) {
  LOG(INFO) << "Change " << (is_relative ? "by" : "to") << ' ' << count << " number of mentions in thread of "
            << forum_topic_id << " in " << dialog_id;
  auto dialog_topics = get_dialog_topics(dialog_id);
  if (dialog_topics == nullptr) {
    return;
  }
  auto topic = get_topic(dialog_topics, forum_topic_id);
  if (topic == nullptr || topic->topic_ == nullptr) {
    return;
  }
  if (topic->topic_->update_unread_mention_count(count, is_relative)) {
    on_forum_topic_changed(dialog_id, topic);
  }
}

void ForumTopicManager::on_topic_reaction_count_changed(DialogId dialog_id, ForumTopicId forum_topic_id, int32 count,
                                                        bool is_relative) {
  LOG(INFO) << "Change " << (is_relative ? "by" : "to") << ' ' << count << " number of reactions in thread of "
            << forum_topic_id << " in " << dialog_id;
  auto dialog_topics = get_dialog_topics(dialog_id);
  if (dialog_topics == nullptr) {
    return;
  }
  auto topic = get_topic(dialog_topics, forum_topic_id);
  if (topic == nullptr || topic->topic_ == nullptr) {
    return;
  }
  if (topic->topic_->update_unread_reaction_count(count, is_relative)) {
    on_forum_topic_changed(dialog_id, topic);
  }
}

void ForumTopicManager::on_topic_poll_vote_count_changed(DialogId dialog_id, ForumTopicId forum_topic_id, int32 count,
                                                         bool is_relative) {
  LOG(INFO) << "Change " << (is_relative ? "by" : "to") << ' ' << count << " number of poll votes in thread of "
            << forum_topic_id << " in " << dialog_id;
  auto dialog_topics = get_dialog_topics(dialog_id);
  if (dialog_topics == nullptr) {
    LOG(INFO) << "Topic list of " << dialog_id << " not found";
    return;
  }
  auto topic = get_topic(dialog_topics, forum_topic_id);
  if (topic == nullptr || topic->topic_ == nullptr) {
    LOG(INFO) << "Topic " << forum_topic_id << " not found";
    return;
  }
  if (topic->topic_->update_unread_poll_vote_count(count, is_relative)) {
    on_forum_topic_changed(dialog_id, topic);
  }
}

void ForumTopicManager::repair_topic_unread_mention_count(DialogId dialog_id, ForumTopicId forum_topic_id) {
  // no need to repair mention count in private chats with bots
  if (is_forum(dialog_id, false).is_error() || can_be_forum_topic_id(forum_topic_id).is_error()) {
    return;
  }

  td_->create_handler<GetForumTopicQuery>(Promise<td_api::object_ptr<td_api::forumTopic>>())
      ->send(dialog_id, forum_topic_id);
}

void ForumTopicManager::repair_topic_unread_reaction_count(DialogId dialog_id, ForumTopicId forum_topic_id) {
  if (is_forum(dialog_id, true).is_error() || can_be_forum_topic_id(forum_topic_id).is_error()) {
    return;
  }

  td_->create_handler<GetForumTopicQuery>(Promise<td_api::object_ptr<td_api::forumTopic>>())
      ->send(dialog_id, forum_topic_id);
}

void ForumTopicManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  dialog_topics_.foreach([&](const DialogId &dialog_id, const unique_ptr<DialogTopics> &dialog_topics) {
    dialog_topics->topics_.foreach([&](const ForumTopicId &forum_topic_id, const unique_ptr<Topic> &topic) {
      if (topic->info_ == nullptr) {
        return;
      }
      updates.push_back(get_update_forum_topic_info_object(topic->info_.get()));
      if (topic->topic_ != nullptr) {
        updates.push_back(get_update_forum_topic_object(dialog_id, topic.get()));
      }
    });
  });
}

void ForumTopicManager::repair_topic_unread_poll_vote_count(DialogId dialog_id, ForumTopicId forum_topic_id) {
  // no need to repair unread vote count in private chats with bots
  if (is_forum(dialog_id, false).is_error() || can_be_forum_topic_id(forum_topic_id).is_error()) {
    return;
  }

  td_->create_handler<GetForumTopicQuery>(Promise<td_api::object_ptr<td_api::forumTopic>>())
      ->send(dialog_id, forum_topic_id);
}

}  // namespace td
