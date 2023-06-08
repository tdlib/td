//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MessageId.h"
#include "td/telegram/StoryFullId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class Td;

struct MessageInputReplyTo {
  MessageId message_id_;
  // or
  StoryFullId story_full_id_;

  bool is_valid() const {
    return message_id_.is_valid() || story_full_id_.is_valid();
  }

  MessageInputReplyTo() = default;

  MessageInputReplyTo(MessageId message_id, StoryFullId story_full_id)
      : message_id_(message_id), story_full_id_(story_full_id) {
    CHECK(!story_full_id_.is_valid() || !message_id_.is_valid());
  }

  explicit MessageInputReplyTo(const td_api::object_ptr<td_api::MessageReplyTo> &reply_to_ptr);

  telegram_api::object_ptr<telegram_api::InputReplyTo> get_input_reply_to(Td *td,
                                                                          MessageId top_thread_message_id) const;
};

StringBuilder &operator<<(StringBuilder &string_builder, const MessageInputReplyTo &input_reply_to);

}  // namespace td
