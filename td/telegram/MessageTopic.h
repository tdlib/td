//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/ForumTopicId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/SavedMessagesTopicId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

namespace td {

class Td;

class MessageTopic {
  enum class Type : int32 { None, Thread, Forum, Monoforum, SavedMessages };
  Type type_ = Type::None;
  DialogId dialog_id_;
  MessageId top_thread_message_id_;
  ForumTopicId forum_topic_id_;
  SavedMessagesTopicId saved_messages_topic_id_;

  friend bool operator==(const MessageTopic &lhs, const MessageTopic &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const MessageTopic &message_topic);

  bool is_general_forum() const {
    return type_ == Type::Forum && forum_topic_id_ == ForumTopicId::general();
  }

 public:
  MessageTopic() = default;

  MessageTopic(Td *td, DialogId dialog_id, bool is_topic_message, MessageId top_thread_message_id,
               SavedMessagesTopicId saved_messages_topic_id);

  static MessageTopic autodetect(Td *td, DialogId dialog_id, MessageId top_thread_message_id);

  static MessageTopic thread(DialogId dialog_id, MessageId top_thread_message_id);

  static MessageTopic forum(DialogId dialog_id, ForumTopicId forum_topic_id);

  static MessageTopic monoforum(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id);

  static MessageTopic saved_messages(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id);

  static Result<MessageTopic> get_message_topic(Td *td, DialogId dialog_id,
                                                const td_api::object_ptr<td_api::MessageTopic> &topic);

  static Result<MessageTopic> get_send_message_topic(Td *td, DialogId dialog_id, MessageTopic &&message_topic);

  static Result<MessageTopic> get_send_message_topic(Td *td, DialogId dialog_id,
                                                     const td_api::object_ptr<td_api::MessageTopic> &topic);

  td_api::object_ptr<td_api::MessageTopic> get_message_topic_object(Td *td) const;

  bool is_empty() const {
    return type_ == Type::None;
  }

  bool is_thread() const {
    return type_ == Type::Thread;
  }

  bool is_forum() const {
    return type_ == Type::Forum;
  }

  bool is_forum_general() const {
    return type_ == Type::Forum && forum_topic_id_ == ForumTopicId::general();
  }

  bool is_monoforum() const {
    return type_ == Type::Monoforum;
  }

  bool is_saved_messages() const {
    return type_ == Type::SavedMessages;
  }

  MessageId get_top_thread_message_id() const {
    CHECK(type_ == Type::Thread);
    return top_thread_message_id_;
  }

  ForumTopicId get_forum_topic_id() const {
    CHECK(type_ == Type::Forum);
    return forum_topic_id_;
  }

  SavedMessagesTopicId get_monoforum_saved_messages_topic_id() const {
    CHECK(type_ == Type::Monoforum);
    return saved_messages_topic_id_;
  }

  MessageId get_implicit_reply_to_message_id(const Td *td) const;

  int32 get_input_top_msg_id() const {
    switch (type_) {
      case Type::Thread:
        return top_thread_message_id_.get_server_message_id().get();
      case Type::Forum:
        return forum_topic_id_.get();
      default:
        return 0;
    }
  }

  telegram_api::object_ptr<telegram_api::InputPeer> get_saved_input_peer(const Td *td) const {
    if (type_ != Type::SavedMessages && type_ != Type::Monoforum) {
      return nullptr;
    }
    auto saved_input_peer = saved_messages_topic_id_.get_input_peer(td);
    CHECK(saved_input_peer != nullptr);
    return saved_input_peer;
  }
};

StringBuilder &operator<<(StringBuilder &string_builder, const MessageTopic &message_topic);

bool operator==(const MessageTopic &lhs, const MessageTopic &rhs);

inline bool operator!=(const MessageTopic &lhs, const MessageTopic &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
