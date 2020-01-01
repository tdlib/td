//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/InputMessageText.h"

#include "td/telegram/MessageEntity.h"

#include "td/utils/common.h"

namespace td {

bool operator==(const InputMessageText &lhs, const InputMessageText &rhs) {
  return lhs.text == rhs.text && lhs.disable_web_page_preview == rhs.disable_web_page_preview &&
         lhs.clear_draft == rhs.clear_draft;
}

bool operator!=(const InputMessageText &lhs, const InputMessageText &rhs) {
  return !(lhs == rhs);
}

Result<InputMessageText> process_input_message_text(const ContactsManager *contacts_manager, DialogId dialog_id,
                                                    tl_object_ptr<td_api::InputMessageContent> &&input_message_content,
                                                    bool is_bot, bool for_draft) {
  CHECK(input_message_content != nullptr);
  CHECK(input_message_content->get_id() == td_api::inputMessageText::ID);
  auto input_message_text = static_cast<td_api::inputMessageText *>(input_message_content.get());
  if (input_message_text->text_ == nullptr) {
    if (for_draft) {
      return InputMessageText{FormattedText(), input_message_text->disable_web_page_preview_,
                              input_message_text->clear_draft_};
    }

    return Status::Error(400, "Message text can't be empty");
  }

  TRY_RESULT(entities, get_message_entities(contacts_manager, std::move(input_message_text->text_->entities_)));
  TRY_STATUS(fix_formatted_text(input_message_text->text_->text_, entities, for_draft, false,
                                need_skip_bot_commands(contacts_manager, dialog_id, is_bot), for_draft));
  return InputMessageText{FormattedText{std::move(input_message_text->text_->text_), std::move(entities)},
                          input_message_text->disable_web_page_preview_, input_message_text->clear_draft_};
}

td_api::object_ptr<td_api::inputMessageText> get_input_message_text_object(const InputMessageText &input_message_text) {
  return td_api::make_object<td_api::inputMessageText>(get_formatted_text_object(input_message_text.text),
                                                       input_message_text.disable_web_page_preview,
                                                       input_message_text.clear_draft);
}

}  // namespace td
