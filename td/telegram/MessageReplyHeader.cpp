//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageReplyHeader.h"

#include "td/telegram/FullMessageId.h"
#include "td/telegram/ScheduledServerMessageId.h"
#include "td/telegram/ServerMessageId.h"

#include "td/utils/logging.h"

namespace td {

MessageReplyHeader::MessageReplyHeader(tl_object_ptr<telegram_api::messageReplyHeader> &&reply_header,
                                       DialogId dialog_id, MessageId message_id, int32 date, bool can_have_thread) {
  if (reply_header == nullptr) {
    return;
  }
  if (reply_header->reply_to_scheduled_) {
    reply_to_message_id = MessageId(ScheduledServerMessageId(reply_header->reply_to_msg_id_), date);
    if (message_id.is_scheduled()) {
      auto reply_to_peer_id = std::move(reply_header->reply_to_peer_id_);
      if (reply_to_peer_id != nullptr) {
        reply_in_dialog_id = DialogId(reply_to_peer_id);
        LOG(ERROR) << "Receive reply to " << FullMessageId{reply_in_dialog_id, reply_to_message_id} << " in "
                   << FullMessageId{dialog_id, message_id};
        reply_to_message_id = MessageId();
        reply_in_dialog_id = DialogId();
      }
    } else {
      LOG(ERROR) << "Receive reply to " << reply_to_message_id << " in " << FullMessageId{dialog_id, message_id};
      reply_to_message_id = MessageId();
    }
  } else {
    reply_to_message_id = MessageId(ServerMessageId(reply_header->reply_to_msg_id_));
    auto reply_to_peer_id = std::move(reply_header->reply_to_peer_id_);
    if (reply_to_peer_id != nullptr) {
      reply_in_dialog_id = DialogId(reply_to_peer_id);
      if (!reply_in_dialog_id.is_valid()) {
        LOG(ERROR) << "Receive reply in invalid " << to_string(reply_to_peer_id);
        reply_to_message_id = MessageId();
        reply_in_dialog_id = DialogId();
      }
      if (reply_in_dialog_id == dialog_id) {
        reply_in_dialog_id = DialogId();  // just in case
      }
    }
    if (reply_to_message_id.is_valid() && !message_id.is_scheduled() && !reply_in_dialog_id.is_valid()) {
      if ((reply_header->flags_ & telegram_api::messageReplyHeader::REPLY_TO_TOP_ID_MASK) != 0) {
        top_thread_message_id = MessageId(ServerMessageId(reply_header->reply_to_top_id_));
      } else if (can_have_thread) {
        top_thread_message_id = reply_to_message_id;
      }
      is_topic_message = reply_header->forum_topic_;
    }
  }
}

}  // namespace td
