//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/RepliedMessageInfo.h"

#include "td/telegram/MessageFullId.h"
#include "td/telegram/ScheduledServerMessageId.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/StoryId.h"
#include "td/telegram/UserId.h"

#include "td/utils/logging.h"

namespace td {

RepliedMessageInfo::RepliedMessageInfo(Td *td, tl_object_ptr<telegram_api::messageReplyHeader> &&reply_header,
                                       DialogId dialog_id, MessageId message_id, int32 date) {
  CHECK(reply_header != nullptr);
  if (reply_header->reply_to_scheduled_) {
    reply_to_message_id_ = MessageId(ScheduledServerMessageId(reply_header->reply_to_msg_id_), date);
    if (message_id.is_scheduled()) {
      auto reply_to_peer_id = std::move(reply_header->reply_to_peer_id_);
      if (reply_to_peer_id != nullptr) {
        reply_in_dialog_id_ = DialogId(reply_to_peer_id);
        LOG(ERROR) << "Receive reply to " << MessageFullId{reply_in_dialog_id_, reply_to_message_id_} << " in "
                   << MessageFullId{dialog_id, message_id};
        reply_to_message_id_ = MessageId();
        reply_in_dialog_id_ = DialogId();
      }
    } else {
      LOG(ERROR) << "Receive reply to " << reply_to_message_id_ << " in " << MessageFullId{dialog_id, message_id};
      reply_to_message_id_ = MessageId();
    }
    if (reply_header->reply_from_ != nullptr || reply_header->reply_media_ != nullptr ||
        !reply_header->quote_text_.empty() || !reply_header->quote_entities_.empty()) {
      LOG(ERROR) << "Receive reply from other chat " << to_string(reply_header) << " in "
                 << MessageFullId{dialog_id, message_id};
    }
  } else {
    if (reply_header->reply_to_msg_id_ != 0) {
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
      if (!reply_to_message_id_.is_valid()) {
        LOG(ERROR) << "Receive " << to_string(reply_header) << " in " << MessageFullId{dialog_id, message_id};
        reply_to_message_id_ = MessageId();
        reply_in_dialog_id_ = DialogId();
      }
    } else if (reply_header->reply_to_peer_id_ != nullptr) {
      LOG(ERROR) << "Receive " << to_string(reply_header) << " in " << MessageFullId{dialog_id, message_id};
    }
    if (reply_header->reply_from_ != nullptr) {
      reply_date_ = reply_header->reply_from_->date_;
      if (reply_header->reply_from_->channel_post_ != 0) {
        LOG(ERROR) << "Receive " << to_string(reply_header) << " in " << MessageFullId{dialog_id, message_id};
      } else {
        auto r_reply_origin = MessageOrigin::get_message_origin(td, std::move(reply_header->reply_from_));
        if (r_reply_origin.is_error()) {
          reply_date_ = 0;
        }
      }
    }
  }
}

MessageId RepliedMessageInfo::get_same_chat_reply_to_message_id() const {
  return is_same_chat_reply() ? reply_to_message_id_ : MessageId();
}

MessageFullId RepliedMessageInfo::get_reply_message_full_id() const {
  return {reply_in_dialog_id_, reply_to_message_id_};
}

}  // namespace td
