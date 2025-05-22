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

  Result<MessageTopic> get_message_topic(Td *td, DialogId dialog_id,
                                         const td_api::object_ptr<td_api::MessageTopic> &topic);

  td_api::object_ptr<td_api::MessageTopic> get_message_topic_object(Td *td) const;
};

StringBuilder &operator<<(StringBuilder &string_builder, const MessageTopic &message_topic);

}  // namespace td
