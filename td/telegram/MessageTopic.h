//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MessageId.h"
#include "td/telegram/SavedMessagesTopicId.h"
#include "td/telegram/td_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class Td;

class MessageTopic {
  enum class Type : int32 { None, Forum, Monoforum, SavedMessages };
  Type type_ = Type::None;
  DialogId dialog_id_;
  MessageId top_thread_message_id_;  // TODO class ForumId
  SavedMessagesTopicId saved_messages_topic_id_;

  friend StringBuilder &operator<<(StringBuilder &string_builder, const MessageTopic &message_topic);

 public:
  MessageTopic() = default;

  MessageTopic(Td *td, DialogId dialog_id, bool is_topic_message, MessageId top_thread_message_id,
               SavedMessagesTopicId saved_messages_topic_id);

  static Result<MessageTopic> get_message_topic(Td *td, DialogId dialog_id,
                                                const td_api::object_ptr<td_api::MessageTopic> &topic);

  td_api::object_ptr<td_api::MessageTopic> get_message_topic_object(Td *td) const;

  bool is_empty() const {
    return type_ == Type::None;
  }

  MessageId get_forum_topic_id() const {
    if (type_ != Type::Forum) {
      return MessageId();
    }
    return top_thread_message_id_;
  }

  SavedMessagesTopicId get_monoforum_topic_id() const {
    if (type_ != Type::Monoforum) {
      return SavedMessagesTopicId();
    }
    return saved_messages_topic_id_;
  }

  SavedMessagesTopicId get_saved_messages_topic_id() const {
    if (type_ != Type::SavedMessages) {
      return SavedMessagesTopicId();
    }
    return saved_messages_topic_id_;
  }
};

StringBuilder &operator<<(StringBuilder &string_builder, const MessageTopic &message_topic);

}  // namespace td
