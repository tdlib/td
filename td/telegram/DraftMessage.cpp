//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DraftMessage.h"

#include "td/telegram/Global.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/misc.h"
#include "td/telegram/ServerMessageId.h"

#include "td/utils/logging.h"

namespace td {

td_api::object_ptr<td_api::draftMessage> get_draft_message_object(const unique_ptr<DraftMessage> &draft_message) {
  if (draft_message == nullptr) {
    return nullptr;
  }
  return td_api::make_object<td_api::draftMessage>(draft_message->reply_to_message_id.get(), draft_message->date,
                                                   get_input_message_text_object(draft_message->input_message_text));
}

unique_ptr<DraftMessage> get_draft_message(ContactsManager *contacts_manager,
                                           telegram_api::object_ptr<telegram_api::DraftMessage> &&draft_message_ptr) {
  if (draft_message_ptr == nullptr) {
    return nullptr;
  }
  auto constructor_id = draft_message_ptr->get_id();
  switch (constructor_id) {
    case telegram_api::draftMessageEmpty::ID:
      return nullptr;
    case telegram_api::draftMessage::ID: {
      auto draft = move_tl_object_as<telegram_api::draftMessage>(draft_message_ptr);
      auto flags = draft->flags_;
      auto result = make_unique<DraftMessage>();
      result->date = draft->date_;
      if ((flags & telegram_api::draftMessage::REPLY_TO_MSG_ID_MASK) != 0) {
        result->reply_to_message_id = MessageId(ServerMessageId(draft->reply_to_msg_id_));
        if (!result->reply_to_message_id.is_valid()) {
          LOG(ERROR) << "Receive " << result->reply_to_message_id << " as reply_to_message_id in the draft";
          result->reply_to_message_id = MessageId();
        }
      }

      auto entities = get_message_entities(contacts_manager, std::move(draft->entities_), "draftMessage");
      auto status = fix_formatted_text(draft->message_, entities, true, true, true, true, true);
      if (status.is_error()) {
        LOG(ERROR) << "Receive error " << status << " while parsing draft " << draft->message_;
        if (!clean_input_string(draft->message_)) {
          draft->message_.clear();
        }
        entities = find_entities(draft->message_, false, true);
      }
      result->input_message_text.text = FormattedText{std::move(draft->message_), std::move(entities)};
      result->input_message_text.disable_web_page_preview = draft->no_webpage_;
      result->input_message_text.clear_draft = false;

      return result;
    }
    default:
      UNREACHABLE();
      return nullptr;
  }
}

Result<unique_ptr<DraftMessage>> get_draft_message(ContactsManager *contacts_manager, DialogId dialog_id,
                                                   td_api::object_ptr<td_api::draftMessage> &&draft_message) {
  if (draft_message == nullptr) {
    return nullptr;
  }

  auto result = make_unique<DraftMessage>();
  result->date = G()->unix_time();
  result->reply_to_message_id = MessageId(draft_message->reply_to_message_id_);
  if (result->reply_to_message_id != MessageId() && !result->reply_to_message_id.is_valid()) {
    return Status::Error(400, "Invalid reply_to_message_id specified");
  }

  auto input_message_content = std::move(draft_message->input_message_text_);
  if (input_message_content != nullptr) {
    if (input_message_content->get_id() != td_api::inputMessageText::ID) {
      return Status::Error(400, "Input message content type must be InputMessageText");
    }
    TRY_RESULT(message_content,
               process_input_message_text(contacts_manager, dialog_id, std::move(input_message_content), false, true));
    result->input_message_text = std::move(message_content);
  }

  return std::move(result);
}

}  // namespace td
