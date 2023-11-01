//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageInputReplyTo.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/Dependencies.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/InputDialogId.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/StoryId.h"
#include "td/telegram/Td.h"
#include "td/telegram/UserId.h"

#include "td/utils/logging.h"

namespace td {

MessageInputReplyTo::~MessageInputReplyTo() = default;

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
        if (!dialog_id.is_valid() || !td->messages_manager_->have_input_peer(dialog_id, AccessRights::Read)) {
          return;
        }
        td->messages_manager_->force_create_dialog(dialog_id, "inputReplyToMessage");
      }
      message_id_ = message_id;
      dialog_id_ = dialog_id;

      if (!reply_to->quote_text_.empty()) {
        auto entities = get_message_entities(td->contacts_manager_.get(), std::move(reply_to->quote_entities_),
                                             "inputReplyToMessage");
        auto status = fix_formatted_text(reply_to->quote_text_, entities, true, true, true, true, false);
        if (status.is_error()) {
          if (!clean_input_string(reply_to->quote_text_)) {
            reply_to->quote_text_.clear();
          }
          entities.clear();
        }
        quote_ = FormattedText{std::move(reply_to->quote_text_), std::move(entities)};
        remove_unallowed_quote_entities(quote_);
      }
      break;
    }
    default:
      UNREACHABLE();
  }
}

void MessageInputReplyTo::add_dependencies(Dependencies &dependencies) const {
  dependencies.add_dialog_and_dependencies(dialog_id_);
  add_formatted_text_dependencies(dependencies, &quote_);                    // just in case
  dependencies.add_dialog_and_dependencies(story_full_id_.get_dialog_id());  // just in case
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
  telegram_api::object_ptr<telegram_api::InputPeer> input_peer;
  if (dialog_id_ != DialogId()) {
    input_peer = td->messages_manager_->get_input_peer(dialog_id_, AccessRights::Read);
    if (input_peer == nullptr) {
      LOG(INFO) << "Failed to get input peer for " << dialog_id_;
      return nullptr;
    }
    flags |= telegram_api::inputReplyToMessage::REPLY_TO_PEER_ID_MASK;
  }
  if (!quote_.text.empty()) {
    flags |= telegram_api::inputReplyToMessage::QUOTE_TEXT_MASK;
  }
  auto quote_entities = get_input_message_entities(td->contacts_manager_.get(), quote_.entities, "get_input_reply_to");
  if (!quote_entities.empty()) {
    flags |= telegram_api::inputReplyToMessage::QUOTE_ENTITIES_MASK;
  }
  return telegram_api::make_object<telegram_api::inputReplyToMessage>(
      flags, reply_to_message_id.get_server_message_id().get(), top_thread_message_id.get_server_message_id().get(),
      std::move(input_peer), quote_.text, std::move(quote_entities));
}

// only for draft messages
td_api::object_ptr<td_api::InputMessageReplyTo> MessageInputReplyTo::get_input_message_reply_to_object(Td *td) const {
  if (story_full_id_.is_valid()) {
    return td_api::make_object<td_api::inputMessageReplyToStory>(
        td->messages_manager_->get_chat_id_object(story_full_id_.get_dialog_id(), "inputMessageReplyToStory"),
        story_full_id_.get_story_id().get());
  }
  if (!message_id_.is_valid() && !message_id_.is_valid_scheduled()) {
    return nullptr;
  }
  td_api::object_ptr<td_api::formattedText> quote;
  if (!quote_.text.empty()) {
    quote = get_formatted_text_object(quote_, true, -1);
  }
  return td_api::make_object<td_api::inputMessageReplyToMessage>(
      td->messages_manager_->get_chat_id_object(dialog_id_, "inputMessageReplyToMessage"), message_id_.get(),
      std::move(quote));
}

MessageId MessageInputReplyTo::get_same_chat_reply_to_message_id() const {
  return dialog_id_ == DialogId() && (message_id_.is_valid() || message_id_.is_valid_scheduled()) ? message_id_
                                                                                                  : MessageId();
}

MessageFullId MessageInputReplyTo::get_reply_message_full_id(DialogId owner_dialog_id) const {
  if (!message_id_.is_valid() && !message_id_.is_valid_scheduled()) {
    return {};
  }
  return {dialog_id_ != DialogId() ? dialog_id_ : owner_dialog_id, message_id_};
}

bool operator==(const MessageInputReplyTo &lhs, const MessageInputReplyTo &rhs) {
  return lhs.message_id_ == rhs.message_id_ && lhs.dialog_id_ == rhs.dialog_id_ &&
         lhs.story_full_id_ == rhs.story_full_id_ && lhs.quote_ == rhs.quote_;
}

bool operator!=(const MessageInputReplyTo &lhs, const MessageInputReplyTo &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const MessageInputReplyTo &input_reply_to) {
  if (input_reply_to.message_id_.is_valid() || input_reply_to.message_id_.is_valid_scheduled()) {
    string_builder << input_reply_to.message_id_;
    if (input_reply_to.dialog_id_ != DialogId()) {
      string_builder << " in " << input_reply_to.dialog_id_;
    }
    if (!input_reply_to.quote_.text.empty()) {
      string_builder << " with " << input_reply_to.quote_.text.size() << " quoted bytes";
    }
    return string_builder;
  }
  if (input_reply_to.story_full_id_.is_valid()) {
    return string_builder << input_reply_to.story_full_id_;
  }
  return string_builder << "nothing";
}

}  // namespace td
