//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
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

Result<InputMessageText> process_input_message_text(const Td *td, DialogId dialog_id,
                                                    tl_object_ptr<td_api::InputMessageContent> &&input_message_content,
                                                    bool is_bot, bool for_draft) {
  CHECK(input_message_content != nullptr);
  CHECK(input_message_content->get_id() == td_api::inputMessageText::ID);
  auto input_message_text = static_cast<td_api::inputMessageText *>(input_message_content.get());
  TRY_RESULT(text, get_formatted_text(td, dialog_id, std::move(input_message_text->text_), is_bot, for_draft, for_draft,
                                      for_draft));
  return InputMessageText{std::move(text), input_message_text->disable_web_page_preview_,
                          input_message_text->clear_draft_};
}

// used only for draft
td_api::object_ptr<td_api::inputMessageText> get_input_message_text_object(const InputMessageText &input_message_text) {
  return td_api::make_object<td_api::inputMessageText>(get_formatted_text_object(input_message_text.text, false, -1),
                                                       input_message_text.disable_web_page_preview,
                                                       input_message_text.clear_draft);
}

}  // namespace td
