//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MessageId.h"

#include "td/utils/common.h"

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
};

}  // namespace td
