// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/RepliedMessageInfo.h"
#include "td/telegram/StoryFullId.h"
#include "td/telegram/telegram_api.h"

namespace td {

class Td;

struct MessageReplyHeader {
  RepliedMessageInfo replied_message_info_;

  MessageId top_thread_message_id_;
  bool is_topic_message_ = false;

  // or

  StoryFullId story_full_id_;

  MessageReplyHeader() = default;

  // Normalizes malformed forum-topic replies to a canonical top-thread anchor form.
  static void normalize_topic_reply_header(telegram_api::messageReplyHeader &reply_header);

  // Finalizes channel topic-thread state after reply parsing with safe fallback repair and fail-closed rejection.
  static bool finalize_channel_topic_thread_state(MessageId message_id, MessageId same_chat_reply_to_message_id,
                                                  MessageId &top_thread_message_id, bool &is_topic_message);

  MessageReplyHeader(Td *td, tl_object_ptr<telegram_api::MessageReplyHeader> &&reply_header_ptr, DialogId dialog_id,
                     MessageId message_id, int32 date);
};

}  // namespace td
