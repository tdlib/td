//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageTopic.h"

#include "td/telegram/ChatManager.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/SavedMessagesManager.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/Td.h"

namespace td {

MessageTopic::MessageTopic(Td *td, DialogId dialog_id, bool is_topic_message, MessageId top_thread_message_id,
                           SavedMessagesTopicId saved_messages_topic_id) {
  if (dialog_id == td->dialog_manager_->get_my_dialog_id()) {
    if (saved_messages_topic_id.is_valid()) {
      type_ = Type::SavedMessages;
      dialog_id_ = dialog_id;
      saved_messages_topic_id_ = saved_messages_topic_id;
    }
    return;
  }
  if (dialog_id.get_type() != DialogType::Channel) {
    return;
  }
  auto channel_id = dialog_id.get_channel_id();
  if (td->chat_manager_->is_monoforum_channel(channel_id)) {
    if (saved_messages_topic_id.is_valid()) {
      type_ = Type::Monoforum;
      dialog_id_ = dialog_id;
      saved_messages_topic_id_ = saved_messages_topic_id;
    }
    return;
  }
  if (td->chat_manager_->is_megagroup_channel(channel_id)) {
    if (is_topic_message) {
      if (top_thread_message_id.is_valid()) {
        type_ = Type::Forum;
        dialog_id_ = dialog_id;
        top_thread_message_id_ = top_thread_message_id;
      }
      return;
    }
    type_ = Type::Forum;
    dialog_id_ = dialog_id;
    top_thread_message_id_ = MessageId(ServerMessageId(1));
  }
}

MessageTopic MessageTopic::forum(DialogId dialog_id, MessageId top_thread_message_id) {
  // dialog_id can be a broadcast channel
  MessageTopic result;
  result.type_ = Type::Forum;
  result.dialog_id_ = dialog_id;
  result.top_thread_message_id_ = top_thread_message_id;
  return result;
}

MessageTopic MessageTopic::monoforum(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id) {
  MessageTopic result;
  result.type_ = Type::Monoforum;
  result.dialog_id_ = dialog_id;
  result.saved_messages_topic_id_ = saved_messages_topic_id;
  return result;
}

MessageTopic MessageTopic::saved_messages(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id) {
  MessageTopic result;
  result.type_ = Type::SavedMessages;
  result.dialog_id_ = dialog_id;
  result.saved_messages_topic_id_ = saved_messages_topic_id;
  return result;
}

Result<MessageTopic> MessageTopic::get_message_topic(Td *td, DialogId dialog_id,
                                                     const td_api::object_ptr<td_api::MessageTopic> &topic) {
  if (topic == nullptr) {
    return MessageTopic();
  }
  if (!td->dialog_manager_->have_dialog_force(dialog_id, "get_message_topic")) {
    return Status::Error(400, "Chat not found");
  }
  MessageTopic result;
  result.dialog_id_ = dialog_id;
  switch (topic->get_id()) {
    case td_api::messageTopicForum::ID: {
      auto top_thread_message_id =
          MessageId(static_cast<const td_api::messageTopicForum *>(topic.get())->forum_topic_id_);
      if (dialog_id.get_type() != DialogType::Channel ||
          !td->chat_manager_->is_megagroup_channel(dialog_id.get_channel_id())) {
        return Status::Error(400, "Chat is not a forum");
      }
      if (!top_thread_message_id.is_server()) {
        return Status::Error(400, "Invalid topic identifier specified");
      }
      // TODO TRY_STATUS(forum_id.is_valid_in(td, dialog_id));
      result.type_ = Type::Forum;
      result.top_thread_message_id_ = top_thread_message_id;
      break;
    }
    case td_api::messageTopicDirectMessages::ID: {
      if (!td->dialog_manager_->is_monoforum_channel(dialog_id)) {
        return Status::Error(400, "Chat is not a channel direct messages chat");
      }
      SavedMessagesTopicId saved_messages_topic_id(DialogId(
          static_cast<const td_api::messageTopicDirectMessages *>(topic.get())->direct_messages_chat_topic_id_));
      TRY_STATUS(saved_messages_topic_id.is_valid_in(td, dialog_id));
      if (!td->saved_messages_manager_->have_topic(dialog_id, saved_messages_topic_id)) {
        return Status::Error(400, "Topic not found");
      }
      result.type_ = Type::Monoforum;
      result.saved_messages_topic_id_ = saved_messages_topic_id;
      break;
    }
    case td_api::messageTopicSavedMessages::ID: {
      if (dialog_id != td->dialog_manager_->get_my_dialog_id()) {
        return Status::Error(400, "Chat is not the Saved Messages chat");
      }
      SavedMessagesTopicId saved_messages_topic_id(
          DialogId(static_cast<const td_api::messageTopicSavedMessages *>(topic.get())->saved_messages_topic_id_));
      TRY_STATUS(saved_messages_topic_id.is_valid_in(td, dialog_id));
      if (!td->saved_messages_manager_->have_topic(dialog_id, saved_messages_topic_id)) {
        return Status::Error(400, "Topic not found");
      }
      result.type_ = Type::SavedMessages;
      result.saved_messages_topic_id_ = saved_messages_topic_id;
      break;
    }
    default:
      UNREACHABLE();
  }
  return result;
}

td_api::object_ptr<td_api::MessageTopic> MessageTopic::get_message_topic_object(Td *td) const {
  switch (type_) {
    case Type::None:
      return nullptr;
    case Type::Forum:
      // TODO send updateForumTopic before sending its identifier
      return td_api::make_object<td_api::messageTopicForum>(top_thread_message_id_.get());
    case Type::Monoforum:
      return td_api::make_object<td_api::messageTopicDirectMessages>(
          td->saved_messages_manager_->get_saved_messages_topic_id_object(dialog_id_, saved_messages_topic_id_));
    case Type::SavedMessages:
      return td_api::make_object<td_api::messageTopicSavedMessages>(
          td->saved_messages_manager_->get_saved_messages_topic_id_object(dialog_id_, saved_messages_topic_id_));
    default:
      UNREACHABLE();
      return nullptr;
  }
}

bool operator==(const MessageTopic &lhs, const MessageTopic &rhs) {
  return lhs.type_ == rhs.type_ && lhs.dialog_id_ == rhs.dialog_id_ &&
         lhs.top_thread_message_id_ == rhs.top_thread_message_id_ &&
         lhs.saved_messages_topic_id_ == rhs.saved_messages_topic_id_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const MessageTopic &message_topic) {
  switch (message_topic.type_) {
    case MessageTopic::Type::None:
      return string_builder << "not a topic";
    case MessageTopic::Type::Forum:
      return string_builder << "Forum[topic " << message_topic.top_thread_message_id_.get_server_message_id().get()
                            << ']';
    case MessageTopic::Type::Monoforum:
      return string_builder << "DirectMessages[" << message_topic.saved_messages_topic_id_ << ']';
    case MessageTopic::Type::SavedMessages:
      return string_builder << "SavedMessages[" << message_topic.saved_messages_topic_id_ << ']';
    default:
      UNREACHABLE();
      return string_builder;
  }
}

}  // namespace td
