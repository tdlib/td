//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"

namespace td {

struct MessageReplyHeader {
  MessageId reply_to_message_id;
  DialogId reply_in_dialog_id;
  MessageId top_thread_message_id;
  bool is_topic_message = false;

  MessageReplyHeader() = default;

  MessageReplyHeader(tl_object_ptr<telegram_api::messageReplyHeader> &&reply_header, DialogId dialog_id,
                     MessageId message_id, int32 date, bool can_have_thread);
};

}  // namespace td
