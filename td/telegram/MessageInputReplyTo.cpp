//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageInputReplyTo.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/StoryId.h"
#include "td/telegram/Td.h"

#include "td/utils/logging.h"

namespace td {

MessageInputReplyTo::MessageInputReplyTo(const td_api::object_ptr<td_api::MessageReplyTo> &reply_to_ptr) {
  if (reply_to_ptr == nullptr) {
    return;
  }
  switch (reply_to_ptr->get_id()) {
    case td_api::messageReplyToMessage::ID: {
      auto reply_to = static_cast<const td_api::messageReplyToMessage *>(reply_to_ptr.get());
      message_id_ = MessageId(reply_to->message_id_);
      break;
    }
    case td_api::messageReplyToStory::ID: {
      auto reply_to = static_cast<const td_api::messageReplyToStory *>(reply_to_ptr.get());
      story_full_id_ = {DialogId(reply_to->story_sender_chat_id_), StoryId(reply_to->story_id_)};
      break;
    }
    default:
      UNREACHABLE();
  }
}

telegram_api::object_ptr<telegram_api::InputReplyTo> MessageInputReplyTo::get_input_reply_to(
    Td *td, MessageId top_thread_message_id) const {
  if (story_full_id_.is_valid()) {
    auto dialog_id = story_full_id_.get_dialog_id();
    CHECK(dialog_id.get_type() == DialogType::User);
    auto r_input_user = td->contacts_manager_->get_input_user(dialog_id.get_user_id());
    if (r_input_user.is_error()) {
      LOG(ERROR) << "Failed to get input user for " << story_full_id_;
      return nullptr;
    }
    return telegram_api::make_object<telegram_api::inputReplyToStory>(r_input_user.move_as_ok(),
                                                                      story_full_id_.get_story_id().get());
  }
  auto reply_to_message_id = message_id_;
  if (reply_to_message_id == MessageId()) {
    if (top_thread_message_id == MessageId()) {
      return nullptr;
    }
    reply_to_message_id = top_thread_message_id;
  }
  CHECK(reply_to_message_id.is_server());
  int32 flags = 0;
  if (top_thread_message_id != MessageId()) {
    CHECK(top_thread_message_id.is_server());
    flags |= telegram_api::inputReplyToMessage::TOP_MSG_ID_MASK;
  }
  return telegram_api::make_object<telegram_api::inputReplyToMessage>(
      flags, reply_to_message_id.get_server_message_id().get(), top_thread_message_id.get_server_message_id().get());
}

StringBuilder &operator<<(StringBuilder &string_builder, const MessageInputReplyTo &input_reply_to) {
  if (input_reply_to.message_id_.is_valid()) {
    return string_builder << input_reply_to.message_id_;
  }
  if (input_reply_to.story_full_id_.is_valid()) {
    return string_builder << input_reply_to.story_full_id_;
  }
  return string_builder << "nothing";
}

}  // namespace td
