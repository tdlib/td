//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageTopic.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ChatManager.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/ForumTopicManager.h"
#include "td/telegram/SavedMessagesManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/UserManager.h"

#include "td/utils/logging.h"

namespace td {

MessageTopic::MessageTopic(Td *td, DialogId dialog_id, bool is_topic_message, MessageId top_thread_message_id,
                           SavedMessagesTopicId saved_messages_topic_id) {
  auto dialog_type = dialog_id.get_type();
  if (dialog_type == DialogType::User) {
    auto user_id = dialog_id.get_user_id();
    if (user_id == td->user_manager_->get_my_id()) {
      if (saved_messages_topic_id.is_valid()) {
        type_ = Type::SavedMessages;
        dialog_id_ = dialog_id;
        saved_messages_topic_id_ = saved_messages_topic_id;
      }
      return;
    }
    if (td->user_manager_->is_user_bot(user_id) || td->auth_manager_->is_bot()) {
      if (is_topic_message) {
        type_ = Type::Forum;
        dialog_id_ = dialog_id;
        forum_topic_id_ = ForumTopicId::from_top_thread_message_id(top_thread_message_id);
        return;
      }
    }
    return;
  }
  if (dialog_type != DialogType::Channel) {
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
  if (td->chat_manager_->is_forum_channel(channel_id) && !is_topic_message) {
    type_ = Type::Forum;
    dialog_id_ = dialog_id;
    forum_topic_id_ = ForumTopicId::general();
    return;
  }
  if (!top_thread_message_id.is_server()) {
    if (top_thread_message_id != MessageId()) {
      LOG(ERROR) << "Have top thread " << top_thread_message_id.is_server();
    }
    return;
  }
  if (is_topic_message && td->chat_manager_->is_megagroup_channel(channel_id)) {
    type_ = Type::Forum;
    dialog_id_ = dialog_id;
    forum_topic_id_ = ForumTopicId::from_top_thread_message_id(top_thread_message_id);
    return;
  }
  if (td->chat_manager_->is_megagroup_channel(channel_id)) {
    type_ = Type::Thread;
    dialog_id_ = dialog_id;
    top_thread_message_id_ = top_thread_message_id;
  }
}

MessageTopic MessageTopic::autodetect(Td *td, DialogId dialog_id, MessageId top_thread_message_id) {
  if (!top_thread_message_id.is_server()) {
    return {};
  }
  auto dialog_type = dialog_id.get_type();
  if (dialog_type == DialogType::User) {
    auto user_id = dialog_id.get_user_id();
    if (user_id != td->user_manager_->get_my_id() &&
        (td->user_manager_->is_user_bot(user_id) || td->auth_manager_->is_bot())) {
      return forum(dialog_id, ForumTopicId::from_top_thread_message_id(top_thread_message_id));
    }
  }
  if (dialog_type == DialogType::Channel) {
    auto channel_id = dialog_id.get_channel_id();
    if (td->chat_manager_->is_forum_channel(channel_id)) {
      return forum(dialog_id, ForumTopicId::from_top_thread_message_id(top_thread_message_id));
    }
    if (td->chat_manager_->is_megagroup_channel(channel_id)) {
      return thread(dialog_id, top_thread_message_id);
    }
  }
  return {};
}

MessageTopic MessageTopic::thread(DialogId dialog_id, MessageId top_thread_message_id) {
  // dialog_id can be a broadcast channel
  MessageTopic result;
  result.type_ = Type::Thread;
  result.dialog_id_ = dialog_id;
  result.top_thread_message_id_ = top_thread_message_id;
  return result;
}

MessageTopic MessageTopic::forum(DialogId dialog_id, ForumTopicId forum_topic_id) {
  MessageTopic result;
  result.type_ = Type::Forum;
  result.dialog_id_ = dialog_id;
  result.forum_topic_id_ = forum_topic_id;
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
    case td_api::messageTopicThread::ID: {
      auto top_thread_message_id =
          MessageId(static_cast<const td_api::messageTopicThread *>(topic.get())->message_thread_id_);
      if (dialog_id.get_type() != DialogType::Channel ||
          !td->chat_manager_->is_megagroup_channel(dialog_id.get_channel_id()) ||
          td->chat_manager_->is_monoforum_channel(dialog_id.get_channel_id())) {
        return Status::Error(400, "Chat doesn't have threads");
      }
      if (!top_thread_message_id.is_server()) {
        return Status::Error(400, "Invalid message thread identifier specified");
      }
      result.type_ = Type::Thread;
      result.top_thread_message_id_ = top_thread_message_id;
      break;
    }
    case td_api::messageTopicForum::ID: {
      auto forum_topic_id = ForumTopicId(static_cast<const td_api::messageTopicForum *>(topic.get())->forum_topic_id_);
      if (!forum_topic_id.is_valid()) {
        return Status::Error(400, "Invalid topic identifier specified");
      }
      if (!td->forum_topic_manager_->can_be_forum(dialog_id)) {
        return Status::Error(400, "Chat is not a forum");
      }
      result.type_ = Type::Forum;
      result.forum_topic_id_ = forum_topic_id;
      break;
    }
    case td_api::messageTopicDirectMessages::ID: {
      if (!td->dialog_manager_->is_admined_monoforum_channel(dialog_id)) {
        return Status::Error(400, "Chat is not an administered channel direct messages chat");
      }
      auto saved_messages_topic_id = td->saved_messages_manager_->get_topic_id(
          DialogId(),  // the topic itself may be not loaded
          static_cast<const td_api::messageTopicDirectMessages *>(topic.get())->direct_messages_chat_topic_id_);
      if (!saved_messages_topic_id.is_valid()) {
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
      auto saved_messages_topic_id = td->saved_messages_manager_->get_topic_id(
          DialogId(),  // the topic itself may be not loaded
          static_cast<const td_api::messageTopicSavedMessages *>(topic.get())->saved_messages_topic_id_);
      if (!saved_messages_topic_id.is_valid()) {
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

Result<MessageTopic> MessageTopic::get_send_message_topic(Td *td, DialogId dialog_id,
                                                          const td_api::object_ptr<td_api::MessageTopic> &topic_id) {
  TRY_RESULT(message_topic, get_message_topic(td, dialog_id, topic_id));
  return get_send_message_topic(td, dialog_id, std::move(message_topic));
}

Result<MessageTopic> MessageTopic::get_send_message_topic(Td *td, DialogId dialog_id, MessageTopic &&message_topic) {
  // topic is required in administered direct messages chats
  if (td->dialog_manager_->is_admined_monoforum_channel(dialog_id) && !message_topic.is_monoforum()) {
    return Status::Error(400, "Channel direct messages topic must be specified");
  }

  // in other chats the topic can be specified implicitly
  if (message_topic.is_empty()) {
    return MessageTopic();
  }

  if (message_topic.is_saved_messages()) {
    return Status::Error(400, "Messages can't be explicitly sent to a Saved Messages topic");
  }

  return std::move(message_topic);
}

td_api::object_ptr<td_api::MessageTopic> MessageTopic::get_message_topic_object(Td *td) const {
  switch (type_) {
    case Type::None:
      return nullptr;
    case Type::Thread:
      return td_api::make_object<td_api::messageTopicThread>(top_thread_message_id_.get());
    case Type::Forum:
      return td_api::make_object<td_api::messageTopicForum>(
          td->forum_topic_manager_->get_forum_topic_id_object(dialog_id_, forum_topic_id_));
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

MessageId MessageTopic::get_implicit_reply_to_message_id(const Td *td) const {
  switch (type_) {
    case Type::Thread:
      return top_thread_message_id_;
    case Type::Forum: {
      auto dialog_type = dialog_id_.get_type();
      if (td->auth_manager_->is_bot() && dialog_type == DialogType::User) {
        return MessageId();
      }
      if (dialog_type == DialogType::Channel && forum_topic_id_ == ForumTopicId::general()) {
        return MessageId();
      }
      return forum_topic_id_.to_top_thread_message_id();
    }
    case Type::Monoforum:
    case Type::SavedMessages:
    case Type::None:
      return MessageId();
    default:
      UNREACHABLE();
      return MessageId();
  }
}

bool operator==(const MessageTopic &lhs, const MessageTopic &rhs) {
  return lhs.type_ == rhs.type_ && lhs.dialog_id_ == rhs.dialog_id_ &&
         lhs.top_thread_message_id_ == rhs.top_thread_message_id_ && lhs.forum_topic_id_ == rhs.forum_topic_id_ &&
         lhs.saved_messages_topic_id_ == rhs.saved_messages_topic_id_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const MessageTopic &message_topic) {
  switch (message_topic.type_) {
    case MessageTopic::Type::None:
      return string_builder << "not a topic";
    case MessageTopic::Type::Thread:
      return string_builder << "Thread[" << message_topic.top_thread_message_id_ << ']';
    case MessageTopic::Type::Forum:
      return string_builder << "ForumTopic[" << message_topic.forum_topic_id_ << ']';
    case MessageTopic::Type::Monoforum:
      return string_builder << "DirectMessagesTopic[" << message_topic.saved_messages_topic_id_ << ']';
    case MessageTopic::Type::SavedMessages:
      return string_builder << "SavedMessagesTopic[" << message_topic.saved_messages_topic_id_ << ']';
    default:
      UNREACHABLE();
      return string_builder;
  }
}

}  // namespace td
