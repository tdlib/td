//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MessageId.h"

#include "td/utils/common.h"

#include <functional>

namespace td {

struct OrderedMessage {
  int32 random_y = 0;

  bool have_previous = false;
  bool have_next = false;

  MessageId message_id;

  unique_ptr<OrderedMessage> left;
  unique_ptr<OrderedMessage> right;
};

struct OrderedMessages {
  unique_ptr<OrderedMessage> messages_;

  OrderedMessage *insert(MessageId message_id);

  void erase(MessageId message_id);

  vector<MessageId> find_older_messages(MessageId max_message_id) const;

  vector<MessageId> find_newer_messages(MessageId min_message_id) const;

  MessageId find_message_by_date(int32 date, const std::function<int32(MessageId)> &get_message_date) const;

  vector<MessageId> find_messages_by_date(int32 min_date, int32 max_date,
                                          const std::function<int32(MessageId)> &get_message_date) const;
};

}  // namespace td
