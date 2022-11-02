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
#include "td/telegram/ForumTopicIcon.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/Td.h"
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
                                        service_message->date_, creator_dialog_id_, true, false);
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

  void send(ChannelId channel_id, MessageId top_thread_message_id, const string &title,
            CustomEmojiId icon_custom_emoji_id) {
    channel_id_ = channel_id;
    top_thread_message_id_ = top_thread_message_id;

    auto input_channel = td_->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);

    int32 flags =
        telegram_api::channels_editForumTopic::TITLE_MASK | telegram_api::channels_editForumTopic::ICON_EMOJI_ID_MASK;
    send_query(G()->net_query_creator().create(
        telegram_api::channels_editForumTopic(flags, std::move(input_channel),
                                              top_thread_message_id.get_server_message_id().get(), title,
                                              icon_custom_emoji_id.get(), false),
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
                                              top_thread_message_id.get_server_message_id().get(), string(), 0,
                                              is_closed),
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

  auto topic_info = add_topic_info(dialog_id, std::move(forum_topic_info));
  CHECK(topic_info != nullptr);
  promise.set_value(topic_info->get_forum_topic_info_object(td_));
}

void ForumTopicManager::edit_forum_topic(DialogId dialog_id, MessageId top_thread_message_id, string &&title,
                                         CustomEmojiId icon_custom_emoji_id, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, is_forum(dialog_id));
  auto channel_id = dialog_id.get_channel_id();

  if (!top_thread_message_id.is_valid() || !top_thread_message_id.is_server()) {
    return promise.set_error(Status::Error(400, "Invalid message thread identifier specified"));
  }

  if (!td_->contacts_manager_->get_channel_permissions(channel_id).can_edit_topics()) {
    auto topic_info = get_topic_info(dialog_id, top_thread_message_id);
    if (topic_info != nullptr && !topic_info->is_outgoing()) {
      return promise.set_error(Status::Error(400, "Not enough rights to edit the topic"));
    }
  }

  auto new_title = clean_name(std::move(title), MAX_FORUM_TOPIC_TITLE_LENGTH);
  if (new_title.empty()) {
    return promise.set_error(Status::Error(400, "Title must be non-empty"));
  }

  td_->create_handler<EditForumTopicQuery>(std::move(promise))
      ->send(channel_id, top_thread_message_id, new_title, icon_custom_emoji_id);
}

void ForumTopicManager::toggle_forum_topic_is_closed(DialogId dialog_id, MessageId top_thread_message_id,
                                                     bool is_closed, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, is_forum(dialog_id));
  auto channel_id = dialog_id.get_channel_id();

  if (!top_thread_message_id.is_valid() || !top_thread_message_id.is_server()) {
    return promise.set_error(Status::Error(400, "Invalid message thread identifier specified"));
  }

  if (!td_->contacts_manager_->get_channel_permissions(channel_id).can_edit_topics()) {
    auto topic_info = get_topic_info(dialog_id, top_thread_message_id);
    if (topic_info != nullptr && !topic_info->is_outgoing()) {
      return promise.set_error(Status::Error(400, "Not enough rights to close or open the topic"));
    }
  }

  td_->create_handler<EditForumTopicQuery>(std::move(promise))->send(channel_id, top_thread_message_id, is_closed);
}

void ForumTopicManager::delete_forum_topic(DialogId dialog_id, MessageId top_thread_message_id,
                                           Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, is_forum(dialog_id));
  auto channel_id = dialog_id.get_channel_id();

  if (!top_thread_message_id.is_valid() || !top_thread_message_id.is_server()) {
    return promise.set_error(Status::Error(400, "Invalid message thread identifier specified"));
  }

  if (!td_->contacts_manager_->get_channel_permissions(channel_id).can_delete_messages()) {
    auto topic_info = get_topic_info(dialog_id, top_thread_message_id);
    if (topic_info != nullptr && !topic_info->is_outgoing()) {
      return promise.set_error(Status::Error(400, "Not enough rights to delete the topic"));
    }
  }

  td_->messages_manager_->delete_topic_history(dialog_id, top_thread_message_id, std::move(promise));
}

void ForumTopicManager::on_forum_topic_edited(DialogId dialog_id, MessageId top_thread_message_id,
                                              const ForumTopicEditedData &edited_data) {
  auto topic_info = get_topic_info(dialog_id, top_thread_message_id);
  if (topic_info == nullptr) {
    return;
  }
  if (topic_info->apply_edited_data(edited_data)) {
    send_update_forum_topic_info(dialog_id, topic_info);
  }
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

ForumTopicInfo *ForumTopicManager::add_topic_info(DialogId dialog_id, unique_ptr<ForumTopicInfo> &&forum_topic_info) {
  CHECK(forum_topic_info != nullptr);
  auto *dialog_info = dialog_topics_.get_pointer(dialog_id);
  if (dialog_info == nullptr) {
    dialog_topics_.set(dialog_id, make_unique<DialogTopics>());
    dialog_info = dialog_topics_.get_pointer(dialog_id);
    CHECK(dialog_info != nullptr);
  }

  MessageId top_thread_message_id = forum_topic_info->get_top_thread_message_id();
  auto topic_info = dialog_info->topic_infos_.get_pointer(top_thread_message_id);
  if (topic_info == nullptr) {
    dialog_info->topic_infos_.set(top_thread_message_id, std::move(forum_topic_info));
    topic_info = get_topic_info(dialog_id, top_thread_message_id);
    CHECK(topic_info != nullptr);
    send_update_forum_topic_info(dialog_id, topic_info);
  }
  return topic_info;
}

ForumTopicInfo *ForumTopicManager::get_topic_info(DialogId dialog_id, MessageId top_thread_message_id) {
  auto *dialog_info = dialog_topics_.get_pointer(dialog_id);
  if (dialog_info == nullptr) {
    return nullptr;
  }
  return dialog_info->topic_infos_.get_pointer(top_thread_message_id);
}

const ForumTopicInfo *ForumTopicManager::get_topic_info(DialogId dialog_id, MessageId top_thread_message_id) const {
  auto *dialog_info = dialog_topics_.get_pointer(dialog_id);
  if (dialog_info == nullptr) {
    return nullptr;
  }
  return dialog_info->topic_infos_.get_pointer(top_thread_message_id);
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

}  // namespace td
