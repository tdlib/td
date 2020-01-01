//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/td_api.h"

#include "td/utils/Status.h"

namespace td {

class ContactsManager;

class InputMessageText {
 public:
  FormattedText text;
  bool disable_web_page_preview = false;
  bool clear_draft = false;
  InputMessageText() = default;
  InputMessageText(FormattedText text, bool disable_web_page_preview, bool clear_draft)
      : text(std::move(text)), disable_web_page_preview(disable_web_page_preview), clear_draft(clear_draft) {
  }
};

bool operator==(const InputMessageText &lhs, const InputMessageText &rhs);
bool operator!=(const InputMessageText &lhs, const InputMessageText &rhs);

Result<InputMessageText> process_input_message_text(const ContactsManager *contacts_manager, DialogId dialog_id,
                                                    tl_object_ptr<td_api::InputMessageContent> &&input_message_content,
                                                    bool is_bot, bool for_draft = false) TD_WARN_UNUSED_RESULT;

td_api::object_ptr<td_api::inputMessageText> get_input_message_text_object(const InputMessageText &input_message_text);

}  // namespace td
