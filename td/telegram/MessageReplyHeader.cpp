//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageReplyHeader.h"

#include "td/telegram/FullMessageId.h"
#include "td/telegram/ScheduledServerMessageId.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/StoryId.h"
#include "td/telegram/UserId.h"

#include "td/utils/logging.h"

namespace td {

MessageReplyHeader::MessageReplyHeader(tl_object_ptr<telegram_api::MessageReplyHeader> &&reply_header_ptr,
                                       DialogId dialog_id, MessageId message_id, int32 date, bool can_have_thread) {
  if (reply_header_ptr == nullptr) {
    return;
  }
  if (reply_header_ptr->get_id() == telegram_api::messageReplyStoryHeader::ID) {
    auto reply_header = telegram_api::move_object_as<telegram_api::messageReplyStoryHeader>(reply_header_ptr);
    UserId user_id(reply_header->user_id_);
    StoryId story_id(reply_header->story_id_);
    if (!user_id.is_valid() || !story_id.is_server()) {
      LOG(ERROR) << "Receive " << to_string(reply_header);
    } else {
      story_full_id_ = {DialogId(user_id), story_id};
    }
    return;
  }
  CHECK(reply_header_ptr->get_id() == telegram_api::messageReplyHeader::ID);
  auto reply_header = telegram_api::move_object_as<telegram_api::messageReplyHeader>(reply_header_ptr);
  if (reply_header->reply_to_scheduled_) {
    reply_to_message_id_ = MessageId(ScheduledServerMessageId(reply_header->reply_to_msg_id_), date);
    if (message_id.is_scheduled()) {
      auto reply_to_peer_id = std::move(reply_header->reply_to_peer_id_);
      if (reply_to_peer_id != nullptr) {
        reply_in_dialog_id_ = DialogId(reply_to_peer_id);
        LOG(ERROR) << "Receive reply to " << FullMessageId{reply_in_dialog_id_, reply_to_message_id_} << " in "
                   << FullMessageId{dialog_id, message_id};
        reply_to_message_id_ = MessageId();
        reply_in_dialog_id_ = DialogId();
      }
    } else {
      LOG(ERROR) << "Receive reply to " << reply_to_message_id_ << " in " << FullMessageId{dialog_id, message_id};
      reply_to_message_id_ = MessageId();
    }
  } else {
    reply_to_message_id_ = MessageId(ServerMessageId(reply_header->reply_to_msg_id_));
    auto reply_to_peer_id = std::move(reply_header->reply_to_peer_id_);
    if (reply_to_peer_id != nullptr) {
      reply_in_dialog_id_ = DialogId(reply_to_peer_id);
      if (!reply_in_dialog_id_.is_valid()) {
        LOG(ERROR) << "Receive reply in invalid " << to_string(reply_to_peer_id);
        reply_to_message_id_ = MessageId();
        reply_in_dialog_id_ = DialogId();
      }
      if (reply_in_dialog_id_ == dialog_id) {
        reply_in_dialog_id_ = DialogId();  // just in case
      }
    }
    if (reply_to_message_id_.is_valid() && !message_id.is_scheduled() && !reply_in_dialog_id_.is_valid() &&
        can_have_thread) {
      if ((reply_header->flags_ & telegram_api::messageReplyHeader::REPLY_TO_TOP_ID_MASK) != 0) {
        top_thread_message_id_ = MessageId(ServerMessageId(reply_header->reply_to_top_id_));
      } else {
        top_thread_message_id_ = reply_to_message_id_;
      }
      is_topic_message_ = reply_header->forum_topic_;
    }
  }
}

}  // namespace td
