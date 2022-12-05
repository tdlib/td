//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ForumTopicManager.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/AuthManager.h"
#include "td/telegram/ChannelId.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/CustomEmojiId.h"
#include "td/telegram/ForumTopic.h"
#include "td/telegram/ForumTopic.hpp"
#include "td/telegram/ForumTopicIcon.h"
#include "td/telegram/ForumTopicInfo.hpp"
#include "td/telegram/Global.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/MessageThreadDb.h"
#include "td/telegram/misc.h"
#include "td/telegram/OptionManager.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UpdatesManager.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"

namespace td {

class CreateForumTopicQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::forumTopicInfo>> promise_;
  ChannelId channel_id_;
  DialogId creator_dialog_id_;
  int64 random_id_;

 public:
  explicit CreateForumTopicQuery(Promise<td_api::object_ptr<td_api::forumTopicInfo>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, const string &title, int32 icon_color, CustomEmojiId icon_custom_emoji_id,
            DialogId as_dialog_id) {
    channel_id_ = channel_id;
    creator_dialog_id_ = DialogId(td_->contacts_manager_->get_my_id());

    int32 flags = 0;
    if (icon_color != -1) {
      flags |= telegram_api::channels_createForumTopic::ICON_COLOR_MASK;
    }
    if (icon_custom_emoji_id.is_valid()) {
      flags |= telegram_api::channels_createForumTopic::ICON_EMOJI_ID_MASK;
    }
    tl_object_ptr<telegram_api::InputPeer> as_input_peer;
    if (as_dialog_id.is_valid()) {
      as_input_peer = td_->messages_manager_->get_input_peer(as_dialog_id, AccessRights::Write);
      if (as_input_peer != nullptr) {
        flags |= telegram_api::channels_createForumTopic::SEND_AS_MASK;
        creator_dialog_id_ = as_dialog_id;
      }
    }

    do {
      random_id_ = Random::secure_int64();
    } while (random_id_ == 0);

    auto input_channel = td_->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);
    send_query(G()->net_query_creator().create(
        telegram_api::channels_createForumTopic(flags, std::move(input_channel), title, icon_color,
                                                icon_custom_emoji_id.get(), random_id_, std::move(as_input_peer)),
        {{channel_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::channels_createForumTopic>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for CreateForumTopicQuery: " << to_string(ptr);
    auto message = UpdatesManager::get_message_by_random_id(ptr.get(), DialogId(channel_id_), random_id_);
    if (message == nullptr || message->get_id() != telegram_api::messageService::ID) {
      LOG(ERROR) << "Receive invalid result for CreateForumTopicQuery: " << to_string(ptr);
      return promise_.set_error(Status::Error(400, "Invalid result received"));
    }
    auto service_message = static_cast<const telegram_api::messageService *>(message);
    if (service_message->action_->get_id() != telegram_api::messageActionTopicCreate::ID) {
      LOG(ERROR) << "Receive invalid result for CreateForumTopicQuery: " << to_string(ptr);
      return promise_.set_error(Status::Error(400, "Invalid result received"));
    }

    auto action = static_cast<const telegram_api::messageActionTopicCreate *>(service_message->action_.get());
    auto forum_topic_info =
        td::make_unique<ForumTopicInfo>(MessageId(ServerMessageId(service_message->id_)), action->title_,
                                        ForumTopicIcon(action->icon_color_, action->icon_emoji_id_),
                                        service_message->date_, creator_dialog_id_, true, false, false);
    td_->updates_manager_->on_get_updates(
        std::move(ptr),
        PromiseCreator::lambda([dialog_id = DialogId(channel_id_), forum_topic_info = std::move(forum_topic_info),
                                promise = std::move(promise_)](Unit result) mutable {
          send_closure(G()->forum_topic_manager(), &ForumTopicManager::on_forum_topic_created, dialog_id,
                       std::move(forum_topic_info), std::move(promise));
        }));
  }

  void on_error(Status status) final {
    td_->contacts_manager_->on_get_channel_error(channel_id_, status, "CreateForumTopicQuery");
    promise_.set_error(std::move(status));
  }
};

class EditForumTopicQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;
  MessageId top_thread_message_id_;

 public:
  explicit EditForumTopicQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, MessageId top_thread_message_id, bool edit_title, const string &title,
            bool edit_custom_emoji_id, CustomEmojiId icon_custom_emoji_id) {
    channel_id_ = channel_id;
    top_thread_message_id_ = top_thread_message_id;

    auto input_channel = td_->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);

    int32 flags = 0;
    if (edit_title) {
      flags |= telegram_api::channels_editForumTopic::TITLE_MASK;
    }
    if (edit_custom_emoji_id) {
      flags |= telegram_api::channels_editForumTopic::ICON_EMOJI_ID_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::channels_editForumTopic(flags, std::move(input_channel),
                                              top_thread_message_id_.get_server_message_id().get(), title,
                                              icon_custom_emoji_id.get(), false, false),
        {{channel_id}}));
  }

  void send(ChannelId channel_id, MessageId top_thread_message_id, bool is_closed) {
    channel_id_ = channel_id;
    top_thread_message_id_ = top_thread_message_id;

    auto input_channel = td_->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);

    int32 flags = telegram_api::channels_editForumTopic::CLOSED_MASK;
    send_query(G()->net_query_creator().create(
        telegram_api::channels_editForumTopic(flags, std::move(input_channel),
                                              top_thread_message_id_.get_server_message_id().get(), string(), 0,
                                              is_closed, false),
        {{channel_id}}));
  }

  void send(ChannelId channel_id, bool is_hidden) {
    channel_id_ = channel_id;
    top_thread_message_id_ = MessageId(ServerMessageId(1));

    auto input_channel = td_->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);

    int32 flags = telegram_api::channels_editForumTopic::HIDDEN_MASK;
    send_query(G()->net_query_creator().create(
        telegram_api::channels_editForumTopic(flags, std::move(input_channel),
                                              top_thread_message_id_.get_server_message_id().get(), string(), 0, false,
                                              is_hidden),
        {{channel_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::channels_editForumTopic>(packet);
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
    td_->contacts_manager_->on_get_channel_error(channel_id_, status, "EditForumTopicQuery");
    promise_.set_error(std::move(status));
  }
};

class GetForumTopicQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::forumTopic>> promise_;
  ChannelId channel_id_;
  MessageId top_thread_message_id_;

 public:
  explicit GetForumTopicQuery(Promise<td_api::object_ptr<td_api::forumTopic>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, MessageId top_thread_message_id) {
    channel_id_ = channel_id;
    top_thread_message_id_ = top_thread_message_id;

    auto input_channel = td_->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);

    send_query(G()->net_query_creator().create(
        telegram_api::channels_getForumTopicsByID(std::move(input_channel),
                                                  {top_thread_message_id_.get_server_message_id().get()}),
        {{channel_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::channels_getForumTopicsByID>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetForumTopicQuery: " << to_string(ptr);

    td_->contacts_manager_->on_get_users(std::move(ptr->users_), "GetForumTopicQuery");
    td_->contacts_manager_->on_get_chats(std::move(ptr->chats_), "GetForumTopicQuery");
    td_->messages_manager_->on_get_messages(std::move(ptr->messages_), true, false, Promise<Unit>(),
                                            "GetForumTopicQuery");
    if (ptr->topics_.size() != 1u) {
      return promise_.set_value(nullptr);
    }

    auto top_thread_message_id =
        td_->forum_topic_manager_->on_get_forum_topic(DialogId(channel_id_), std::move(ptr->topics_[0]));
    if (!top_thread_message_id.is_valid()) {
      return promise_.set_value(nullptr);
    }
    if (top_thread_message_id != top_thread_message_id_) {
      return promise_.set_error(Status::Error(500, "Wrong forum topic received"));
    }
    promise_.set_value(td_->forum_topic_manager_->get_forum_topic_object(DialogId(channel_id_), top_thread_message_id));
  }

  void on_error(Status status) final {
    td_->contacts_manager_->on_get_channel_error(channel_id_, status, "GetForumTopicQuery");
    promise_.set_error(std::move(status));
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
}

ForumTopicManager::~ForumTopicManager() {
  Scheduler::instance()->destroy_on_scheduler(G()->get_gc_scheduler_id(), dialog_topics_);
}

void ForumTopicManager::tear_down() {
  parent_.reset();
}

void ForumTopicManager::create_forum_topic(DialogId dialog_id, string &&title,
                                           td_api::object_ptr<td_api::forumTopicIcon> &&icon,
                                           Promise<td_api::object_ptr<td_api::forumTopicInfo>> &&promise) {
  TRY_STATUS_PROMISE(promise, is_forum(dialog_id));
  auto channel_id = dialog_id.get_channel_id();

  if (!td_->contacts_manager_->get_channel_permissions(channel_id).can_create_topics()) {
    return promise.set_error(Status::Error(400, "Not enough rights to create a topic"));
  }

  auto new_title = clean_name(std::move(title), MAX_FORUM_TOPIC_TITLE_LENGTH);
  if (new_title.empty()) {
    return promise.set_error(Status::Error(400, "Title must be non-empty"));
  }

  int32 icon_color = -1;
  CustomEmojiId icon_custom_emoji_id;
  if (icon != nullptr) {
    icon_color = icon->color_;
    if (icon_color < 0 || icon_color > 0xFFFFFF) {
      return promise.set_error(Status::Error(400, "Invalid icon color specified"));
    }
    icon_custom_emoji_id = CustomEmojiId(icon->custom_emoji_id_);
  }

  DialogId as_dialog_id = td_->messages_manager_->get_dialog_default_send_message_as_dialog_id(dialog_id);

  td_->create_handler<CreateForumTopicQuery>(std::move(promise))
      ->send(channel_id, new_title, icon_color, icon_custom_emoji_id, as_dialog_id);
}

void ForumTopicManager::on_forum_topic_created(DialogId dialog_id, unique_ptr<ForumTopicInfo> &&forum_topic_info,
                                               Promise<td_api::object_ptr<td_api::forumTopicInfo>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  CHECK(forum_topic_info != nullptr);
  MessageId top_thread_message_id = forum_topic_info->get_top_thread_message_id();
  auto topic = add_topic(dialog_id, top_thread_message_id);
  if (topic->info_ == nullptr) {
    topic->info_ = std::move(forum_topic_info);
    send_update_forum_topic_info(dialog_id, topic->info_.get());
    save_topic_to_database(dialog_id, topic);
  }
  promise.set_value(topic->info_->get_forum_topic_info_object(td_));
}

void ForumTopicManager::edit_forum_topic(DialogId dialog_id, MessageId top_thread_message_id, string &&title,
                                         bool edit_icon_custom_emoji, CustomEmojiId icon_custom_emoji_id,
                                         Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, is_forum(dialog_id));
  TRY_STATUS_PROMISE(promise, can_be_message_thread_id(top_thread_message_id));
  auto channel_id = dialog_id.get_channel_id();

  if (!td_->contacts_manager_->get_channel_permissions(channel_id).can_edit_topics()) {
    auto topic_info = get_topic_info(dialog_id, top_thread_message_id);
    if (topic_info != nullptr && !topic_info->is_outgoing()) {
      return promise.set_error(Status::Error(400, "Not enough rights to edit the topic"));
    }
  }

  bool edit_title = !title.empty();
  auto new_title = clean_name(std::move(title), MAX_FORUM_TOPIC_TITLE_LENGTH);
  if (edit_title && new_title.empty()) {
    return promise.set_error(Status::Error(400, "Title must be non-empty"));
  }
  if (!edit_title && !edit_icon_custom_emoji) {
    return promise.set_value(Unit());
  }

  td_->create_handler<EditForumTopicQuery>(std::move(promise))
      ->send(channel_id, top_thread_message_id, edit_title, new_title, edit_icon_custom_emoji, icon_custom_emoji_id);
}

void ForumTopicManager::get_forum_topic(DialogId dialog_id, MessageId top_thread_message_id,
                                        Promise<td_api::object_ptr<td_api::forumTopic>> &&promise) {
  TRY_STATUS_PROMISE(promise, is_forum(dialog_id));
  TRY_STATUS_PROMISE(promise, can_be_message_thread_id(top_thread_message_id));
  auto channel_id = dialog_id.get_channel_id();

  td_->create_handler<GetForumTopicQuery>(std::move(promise))->send(channel_id, top_thread_message_id);
}

void ForumTopicManager::get_forum_topic_link(DialogId dialog_id, MessageId top_thread_message_id,
                                             Promise<string> &&promise) {
  TRY_STATUS_PROMISE(promise, is_forum(dialog_id));
  TRY_STATUS_PROMISE(promise, can_be_message_thread_id(top_thread_message_id));
  auto channel_id = dialog_id.get_channel_id();

  SliceBuilder sb;
  sb << td_->option_manager_->get_option_string("t_me_url", "https://t.me/");

  auto dialog_username = td_->contacts_manager_->get_channel_first_username(channel_id);
  if (!dialog_username.empty()) {
    sb << dialog_username;
  } else {
    sb << "c/" << channel_id.get();
  }
  sb << '/' << top_thread_message_id.get_server_message_id().get();

  promise.set_value(sb.as_cslice().str());
}

void ForumTopicManager::toggle_forum_topic_is_closed(DialogId dialog_id, MessageId top_thread_message_id,
                                                     bool is_closed, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, is_forum(dialog_id));
  TRY_STATUS_PROMISE(promise, can_be_message_thread_id(top_thread_message_id));
  auto channel_id = dialog_id.get_channel_id();

  if (!td_->contacts_manager_->get_channel_permissions(channel_id).can_edit_topics()) {
    auto topic_info = get_topic_info(dialog_id, top_thread_message_id);
    if (topic_info != nullptr && !topic_info->is_outgoing()) {
      return promise.set_error(Status::Error(400, "Not enough rights to close or open the topic"));
    }
  }

  td_->create_handler<EditForumTopicQuery>(std::move(promise))->send(channel_id, top_thread_message_id, is_closed);
}

void ForumTopicManager::toggle_forum_topic_is_hidden(DialogId dialog_id, bool is_hidden, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, is_forum(dialog_id));
  auto channel_id = dialog_id.get_channel_id();

  if (!td_->contacts_manager_->get_channel_permissions(channel_id).can_edit_topics()) {
    return promise.set_error(Status::Error(400, "Not enough rights to close or open the topic"));
  }

  td_->create_handler<EditForumTopicQuery>(std::move(promise))->send(channel_id, is_hidden);
}

void ForumTopicManager::delete_forum_topic(DialogId dialog_id, MessageId top_thread_message_id,
                                           Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, is_forum(dialog_id));
  TRY_STATUS_PROMISE(promise, can_be_message_thread_id(top_thread_message_id));
  auto channel_id = dialog_id.get_channel_id();

  if (!td_->contacts_manager_->get_channel_permissions(channel_id).can_delete_messages()) {
    auto topic_info = get_topic_info(dialog_id, top_thread_message_id);
    if (topic_info != nullptr && !topic_info->is_outgoing()) {
      return promise.set_error(Status::Error(400, "Not enough rights to delete the topic"));
    }
  }

  auto delete_promise = PromiseCreator::lambda([actor_id = actor_id(this), dialog_id, top_thread_message_id,
                                                promise = std::move(promise)](Result<Unit> result) mutable {
    if (result.is_error()) {
      return promise.set_error(result.move_as_error());
    }
    send_closure(actor_id, &ForumTopicManager::on_delete_forum_topic, dialog_id, top_thread_message_id,
                 std::move(promise));
  });
  td_->messages_manager_->delete_topic_history(dialog_id, top_thread_message_id, std::move(delete_promise));
}

void ForumTopicManager::on_delete_forum_topic(DialogId dialog_id, MessageId top_thread_message_id,
                                              Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  auto *dialog_topics = dialog_topics_.get_pointer(dialog_id);
  if (dialog_topics != nullptr) {
    dialog_topics->topics_.erase(top_thread_message_id);
  }
  delete_topic_from_database(dialog_id, top_thread_message_id, std::move(promise));
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

void ForumTopicManager::on_forum_topic_edited(DialogId dialog_id, MessageId top_thread_message_id,
                                              const ForumTopicEditedData &edited_data) {
  auto topic = get_topic(dialog_id, top_thread_message_id);
  if (topic == nullptr || topic->info_ == nullptr) {
    return;
  }
  if (topic->info_->apply_edited_data(edited_data)) {
    send_update_forum_topic_info(dialog_id, topic->info_.get());
    save_topic_to_database(dialog_id, topic);
  }
}

void ForumTopicManager::on_get_forum_topic_info(DialogId dialog_id, const ForumTopicInfo &topic_info,
                                                const char *source) {
  if (!can_be_forum(dialog_id)) {
    LOG(ERROR) << "Receive forum topics in " << dialog_id << " from " << source;
    return;
  }

  auto dialog_topics = add_dialog_topics(dialog_id);
  CHECK(dialog_topics != nullptr);
  auto forum_topic_info = td::make_unique<ForumTopicInfo>(topic_info);
  MessageId top_thread_message_id = forum_topic_info->get_top_thread_message_id();
  CHECK(can_be_message_thread_id(top_thread_message_id).is_ok());
  auto topic = add_topic(dialog_topics, top_thread_message_id);
  if (set_topic_info(dialog_id, topic, std::move(forum_topic_info))) {
    save_topic_to_database(dialog_id, topic);
  }
}

void ForumTopicManager::on_get_forum_topic_infos(DialogId dialog_id,
                                                 vector<tl_object_ptr<telegram_api::ForumTopic>> &&forum_topics,
                                                 const char *source) {
  if (forum_topics.empty()) {
    return;
  }
  if (!can_be_forum(dialog_id)) {
    LOG(ERROR) << "Receive forum topics in " << dialog_id << " from " << source;
    return;
  }

  auto dialog_topics = add_dialog_topics(dialog_id);
  CHECK(dialog_topics != nullptr);
  for (auto &forum_topic : forum_topics) {
    auto forum_topic_info = td::make_unique<ForumTopicInfo>(forum_topic);
    MessageId top_thread_message_id = forum_topic_info->get_top_thread_message_id();
    if (can_be_message_thread_id(top_thread_message_id).is_error()) {
      continue;
    }
    auto topic = add_topic(dialog_topics, top_thread_message_id);
    if (set_topic_info(dialog_id, topic, std::move(forum_topic_info))) {
      save_topic_to_database(dialog_id, topic);
    }
  }
}

MessageId ForumTopicManager::on_get_forum_topic(DialogId dialog_id,
                                                tl_object_ptr<telegram_api::ForumTopic> &&forum_topic) {
  CHECK(forum_topic != nullptr);
  switch (forum_topic->get_id()) {
    case telegram_api::forumTopicDeleted::ID: {
      auto top_thread_message_id =
          MessageId(ServerMessageId(static_cast<const telegram_api::forumTopicDeleted *>(forum_topic.get())->id_));
      if (!top_thread_message_id.is_valid()) {
        LOG(ERROR) << "Receive " << to_string(forum_topic);
        return MessageId();
      }
      on_delete_forum_topic(dialog_id, top_thread_message_id, Promise<Unit>());
      return MessageId();
    }
    case telegram_api::forumTopic::ID: {
      auto forum_topic_info = td::make_unique<ForumTopicInfo>(forum_topic);
      auto forum_topic_full = td::make_unique<ForumTopic>(td_, std::move(forum_topic));
      if (forum_topic_full->is_short()) {
        LOG(ERROR) << "Receive short " << to_string(forum_topic);
        return MessageId();
      }
      MessageId top_thread_message_id = forum_topic_info->get_top_thread_message_id();
      Topic *topic = add_topic(dialog_id, top_thread_message_id);
      bool need_save = false;
      if (topic->topic_ == nullptr || true) {
        topic->topic_ = std::move(forum_topic_full);
        need_save = true;
      }
      if (set_topic_info(dialog_id, topic, std::move(forum_topic_info))) {
        need_save = true;
      }
      if (need_save) {
        save_topic_to_database(dialog_id, topic);
      }
      return top_thread_message_id;
    }
    default:
      UNREACHABLE();
      return MessageId();
  }
}

td_api::object_ptr<td_api::forumTopic> ForumTopicManager::get_forum_topic_object(
    DialogId dialog_id, MessageId top_thread_message_id) const {
  auto topic = get_topic(dialog_id, top_thread_message_id);
  if (topic == nullptr || topic->topic_ == nullptr) {
    return nullptr;
  }
  CHECK(topic->info_ != nullptr);
  return topic->topic_->get_forum_topic_object(td_, dialog_id, *topic->info_);
}

Status ForumTopicManager::is_forum(DialogId dialog_id) {
  if (!td_->messages_manager_->have_dialog_force(dialog_id, "ForumTopicManager::is_forum")) {
    return Status::Error(400, "Chat not found");
  }
  if (dialog_id.get_type() != DialogType::Channel ||
      !td_->contacts_manager_->is_forum_channel(dialog_id.get_channel_id())) {
    return Status::Error(400, "The chat is not a forum");
  }
  return Status::OK();
}

bool ForumTopicManager::can_be_forum(DialogId dialog_id) const {
  return dialog_id.get_type() == DialogType::Channel &&
         td_->contacts_manager_->is_megagroup_channel(dialog_id.get_channel_id());
}

Status ForumTopicManager::can_be_message_thread_id(MessageId top_thread_message_id) {
  if (!top_thread_message_id.is_valid() || !top_thread_message_id.is_server()) {
    return Status::Error(400, "Invalid message thread identifier specified");
  }
  return Status::OK();
}

ForumTopicManager::DialogTopics *ForumTopicManager::add_dialog_topics(DialogId dialog_id) {
  auto *dialog_topics = dialog_topics_.get_pointer(dialog_id);
  if (dialog_topics == nullptr) {
    auto new_dialog_topics = make_unique<DialogTopics>();
    dialog_topics = new_dialog_topics.get();
    dialog_topics_.set(dialog_id, std::move(new_dialog_topics));
  }
  return dialog_topics;
}

ForumTopicManager::Topic *ForumTopicManager::add_topic(DialogTopics *dialog_topics, MessageId top_thread_message_id) {
  auto topic = dialog_topics->topics_.get_pointer(top_thread_message_id);
  if (topic == nullptr) {
    auto new_topic = make_unique<Topic>();
    topic = new_topic.get();
    dialog_topics->topics_.set(top_thread_message_id, std::move(new_topic));
  }
  return topic;
}

ForumTopicManager::Topic *ForumTopicManager::add_topic(DialogId dialog_id, MessageId top_thread_message_id) {
  return add_topic(add_dialog_topics(dialog_id), top_thread_message_id);
}

ForumTopicManager::Topic *ForumTopicManager::get_topic(DialogId dialog_id, MessageId top_thread_message_id) {
  auto *dialog_topics = dialog_topics_.get_pointer(dialog_id);
  if (dialog_topics == nullptr) {
    return nullptr;
  }
  return dialog_topics->topics_.get_pointer(top_thread_message_id);
}

const ForumTopicManager::Topic *ForumTopicManager::get_topic(DialogId dialog_id,
                                                             MessageId top_thread_message_id) const {
  auto *dialog_topics = dialog_topics_.get_pointer(dialog_id);
  if (dialog_topics == nullptr) {
    return nullptr;
  }
  return dialog_topics->topics_.get_pointer(top_thread_message_id);
}

ForumTopicInfo *ForumTopicManager::get_topic_info(DialogId dialog_id, MessageId top_thread_message_id) {
  auto *topic = get_topic(dialog_id, top_thread_message_id);
  if (topic == nullptr) {
    return nullptr;
  }
  return topic->info_.get();
}

const ForumTopicInfo *ForumTopicManager::get_topic_info(DialogId dialog_id, MessageId top_thread_message_id) const {
  auto *topic = get_topic(dialog_id, top_thread_message_id);
  if (topic == nullptr) {
    return nullptr;
  }
  return topic->info_.get();
}

bool ForumTopicManager::set_topic_info(DialogId dialog_id, Topic *topic, unique_ptr<ForumTopicInfo> forum_topic_info) {
  if (topic->info_ == nullptr || *topic->info_ != *forum_topic_info) {
    topic->info_ = std::move(forum_topic_info);
    send_update_forum_topic_info(dialog_id, topic->info_.get());
    return true;
  }
  return false;
}

td_api::object_ptr<td_api::updateForumTopicInfo> ForumTopicManager::get_update_forum_topic_info(
    DialogId dialog_id, const ForumTopicInfo *topic_info) const {
  return td_api::make_object<td_api::updateForumTopicInfo>(dialog_id.get(),
                                                           topic_info->get_forum_topic_info_object(td_));
}

void ForumTopicManager::send_update_forum_topic_info(DialogId dialog_id, const ForumTopicInfo *topic_info) const {
  if (td_->auth_manager_->is_bot()) {
    return;
  }
  send_closure(G()->td(), &Td::send_update, get_update_forum_topic_info(dialog_id, topic_info));
}

void ForumTopicManager::save_topic_to_database(DialogId dialog_id, const Topic *topic) {
  if (topic->info_ == nullptr) {
    return;
  }

  auto message_thread_db = G()->td_db()->get_message_thread_db_async();
  if (message_thread_db == nullptr) {
    return;
  }

  auto top_thread_message_id = topic->info_->get_top_thread_message_id();
  LOG(INFO) << "Save topic of " << top_thread_message_id << " in " << dialog_id << " to database";
  message_thread_db->add_message_thread(dialog_id, top_thread_message_id, 0, log_event_store(*topic), Auto());
}

void ForumTopicManager::delete_topic_from_database(DialogId dialog_id, MessageId top_thread_message_id,
                                                   Promise<Unit> &&promise) {
  auto message_thread_db = G()->td_db()->get_message_thread_db_async();
  if (message_thread_db == nullptr) {
    return;
  }

  LOG(INFO) << "Delete topic of " << top_thread_message_id << " in " << dialog_id << " from database";
  message_thread_db->delete_message_thread(dialog_id, top_thread_message_id, Auto());
}

void ForumTopicManager::on_topic_message_count_changed(DialogId dialog_id, MessageId top_thread_message_id, int diff) {
  if (!can_be_forum(dialog_id) || can_be_message_thread_id(top_thread_message_id).is_error()) {
    LOG(ERROR) << "Change by " << diff << " number of loaded messages in thread of " << top_thread_message_id << " in "
               << dialog_id;
    return;
  }

  auto dialog_topics = add_dialog_topics(dialog_id);
  auto topic = add_topic(dialog_topics, top_thread_message_id);
  topic->message_count_ += diff;
  CHECK(topic->message_count_ >= 0);
  if (topic->message_count_ == 0) {
    // TODO keep topics in the topic list
    dialog_topics->topics_.erase(top_thread_message_id);
  }
}

}  // namespace td
