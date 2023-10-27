//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageInputReplyTo.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/InputDialogId.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/StoryId.h"
#include "td/telegram/Td.h"

#include "td/utils/logging.h"

namespace td {
/*
MessageInputReplyTo::MessageInputReplyTo(const td_api::object_ptr<td_api::InputMessageReplyTo> &reply_to_ptr) {
  if (reply_to_ptr == nullptr) {
    return;
  }
  switch (reply_to_ptr->get_id()) {
    case td_api::inputMessageReplyToMessage::ID: {
      auto reply_to = static_cast<const td_api::inputMessageReplyToMessage *>(reply_to_ptr.get());
      message_id_ = MessageId(reply_to->message_id_);
      break;
    }
    case td_api::inputMessageReplyToStory::ID: {
      auto reply_to = static_cast<const td_api::inputMessageReplyToStory *>(reply_to_ptr.get());
      story_full_id_ = {DialogId(reply_to->story_sender_chat_id_), StoryId(reply_to->story_id_)};
      break;
    }
    default:
      UNREACHABLE();
  }
}
*/

MessageInputReplyTo::MessageInputReplyTo(Td *td,
                                         telegram_api::object_ptr<telegram_api::InputReplyTo> &&input_reply_to) {
  if (input_reply_to == nullptr) {
    return;
  }
  switch (input_reply_to->get_id()) {
    case telegram_api::inputReplyToStory::ID: {
      auto reply_to = telegram_api::move_object_as<telegram_api::inputReplyToStory>(input_reply_to);
      if (reply_to->user_id_->get_id() != telegram_api::inputUser::ID) {
        return;
      }
      auto user_id = UserId(static_cast<telegram_api::inputUser *>(reply_to->user_id_.get())->user_id_);
      auto story_id = StoryId(reply_to->story_id_);
      if (user_id.is_valid() && story_id.is_valid()) {
        DialogId dialog_id(user_id);
        td->messages_manager_->force_create_dialog(dialog_id, "MessageInputReplyTo", true);
        story_full_id_ = {dialog_id, story_id};
      }
      break;
    }
    case telegram_api::inputReplyToMessage::ID: {
      auto reply_to = telegram_api::move_object_as<telegram_api::inputReplyToMessage>(input_reply_to);
      MessageId message_id(ServerMessageId(reply_to->reply_to_msg_id_));
      if (!message_id.is_valid() && !message_id_.is_valid_scheduled()) {
        return;
      }
      DialogId dialog_id;
      if (reply_to->reply_to_peer_id_ != nullptr) {
        dialog_id = InputDialogId(reply_to->reply_to_peer_id_).get_dialog_id();
        if (!dialog_id.is_valid()) {
          return;
        }
      }
      // TODO quote_text:flags.2?string quote_entities:flags.3?Vector<MessageEntity>
      message_id_ = message_id;
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
      flags, reply_to_message_id.get_server_message_id().get(), top_thread_message_id.get_server_message_id().get(),
      nullptr, string(), Auto());
}

td_api::object_ptr<td_api::InputMessageReplyTo> MessageInputReplyTo::get_input_message_reply_to_object(
    Td *td, DialogId dialog_id) const {
  CHECK(dialog_id.is_valid());
  if (story_full_id_.is_valid()) {
    return td_api::make_object<td_api::inputMessageReplyToStory>(
        td->messages_manager_->get_chat_id_object(story_full_id_.get_dialog_id(), "inputMessageReplyToStory"),
        story_full_id_.get_story_id().get());
  }
  if (!message_id_.is_valid() && !message_id_.is_valid_scheduled()) {
    return nullptr;
  }
  return td_api::make_object<td_api::inputMessageReplyToMessage>(message_id_.get());
}

MessageId MessageInputReplyTo::get_same_chat_reply_to_message_id() const {
  return is_same_chat_reply() ? message_id_ : MessageId();
}

MessageFullId MessageInputReplyTo::get_reply_message_full_id(DialogId owner_dialog_id) const {
  if (!message_id_.is_valid() && !message_id_.is_valid_scheduled()) {
    return {};
  }
  return {owner_dialog_id, message_id_};
}

bool operator==(const MessageInputReplyTo &lhs, const MessageInputReplyTo &rhs) {
  return lhs.message_id_ == rhs.message_id_ && lhs.story_full_id_ == rhs.story_full_id_;
}

bool operator!=(const MessageInputReplyTo &lhs, const MessageInputReplyTo &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const MessageInputReplyTo &input_reply_to) {
  if (input_reply_to.message_id_.is_valid() || input_reply_to.message_id_.is_valid_scheduled()) {
    return string_builder << input_reply_to.message_id_;
  }
  if (input_reply_to.story_full_id_.is_valid()) {
    return string_builder << input_reply_to.story_full_id_;
  }
  return string_builder << "nothing";
}

}  // namespace td
