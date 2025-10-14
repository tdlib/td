//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageReplyHeader.h"

#include "td/telegram/DialogManager.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/StoryId.h"
#include "td/telegram/Td.h"

#include "td/utils/logging.h"

namespace td {

MessageReplyHeader::MessageReplyHeader(Td *td, tl_object_ptr<telegram_api::MessageReplyHeader> &&reply_header_ptr,
                                       DialogId dialog_id, MessageId message_id, int32 date) {
  if (reply_header_ptr == nullptr) {
    return;
  }
  if (reply_header_ptr->get_id() == telegram_api::messageReplyStoryHeader::ID) {
    auto reply_header = telegram_api::move_object_as<telegram_api::messageReplyStoryHeader>(reply_header_ptr);
    DialogId story_dialog_id(reply_header->peer_);
    StoryId story_id(reply_header->story_id_);
    if (!story_dialog_id.is_valid() || !story_id.is_server()) {
      LOG(ERROR) << "Receive " << to_string(reply_header);
    } else {
      story_full_id_ = {story_dialog_id, story_id};
    }
    return;
  }
  CHECK(reply_header_ptr->get_id() == telegram_api::messageReplyHeader::ID);
  auto reply_header = telegram_api::move_object_as<telegram_api::messageReplyHeader>(reply_header_ptr);

  bool can_have_thread = td->dialog_manager_->can_dialog_have_threads(dialog_id);
  if (!message_id.is_scheduled() && can_have_thread) {
    is_topic_message_ = reply_header->forum_topic_;
    if (reply_header->reply_to_top_id_ != 0) {
      top_thread_message_id_ = MessageId(ServerMessageId(reply_header->reply_to_top_id_));
      if (!top_thread_message_id_.is_valid()) {
        LOG(ERROR) << "Receive " << to_string(reply_header);
        top_thread_message_id_ = MessageId();
      } else if (dialog_id.get_type() == DialogType::User) {
        is_topic_message_ = true;
      }
    }
  }

  replied_message_info_ = RepliedMessageInfo(td, std::move(reply_header), dialog_id, message_id, date);

  if (!message_id.is_scheduled() && can_have_thread && dialog_id.get_type() == DialogType::Channel) {
    if (!top_thread_message_id_.is_valid()) {
      auto same_chat_reply_to_message_id = replied_message_info_.get_same_chat_reply_to_message_id(false);
      if (same_chat_reply_to_message_id.is_valid()) {
        CHECK(same_chat_reply_to_message_id.is_server());
        top_thread_message_id_ = same_chat_reply_to_message_id;
      } else {
        is_topic_message_ = false;
      }
    }
    if (top_thread_message_id_ >= message_id) {
      LOG(ERROR) << "Receive top thread " << top_thread_message_id_ << " in " << message_id << " in " << dialog_id;
      top_thread_message_id_ = MessageId();
    }
  }
}

}  // namespace td
