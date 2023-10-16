//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/InputMessageText.h"

#include "td/telegram/MessageEntity.h"
#include "td/telegram/misc.h"

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
  string web_page_url;
  bool disable_web_page_preview = false;
  bool force_small_media = false;
  bool force_large_media = false;
  if (input_message_text->link_preview_options_ != nullptr) {
    auto options = std::move(input_message_text->link_preview_options_);
    web_page_url = std::move(options->url_);
    disable_web_page_preview = options->is_disabled_;
    force_small_media = options->force_small_media_;
    force_large_media = options->force_large_media_;

    if (!clean_input_string(web_page_url)) {
      return Status::Error(400, "Link preview URL must be encoded in UTF-8");
    }
  }
  return InputMessageText{std::move(text),   std::move(web_page_url), disable_web_page_preview,
                          force_small_media, force_large_media,       input_message_text->clear_draft_};
}

telegram_api::object_ptr<telegram_api::InputMedia> InputMessageText::get_input_media_web_page() const {
  if (web_page_url.empty() && !force_small_media && !force_large_media) {
    return nullptr;
  }
  int32 flags = 0;
  if (force_small_media) {
    flags |= telegram_api::inputMediaWebPage::FORCE_SMALL_MEDIA_MASK;
  }
  if (force_large_media) {
    flags |= telegram_api::inputMediaWebPage::FORCE_LARGE_MEDIA_MASK;
  }
  if (!text.text.empty()) {
    flags |= telegram_api::inputMediaWebPage::OPTIONAL_MASK;
  }
  return telegram_api::make_object<telegram_api::inputMediaWebPage>(flags, false /*ignored*/, false /*ignored*/,
                                                                    false /*ignored*/, web_page_url);
}

// used only for draft
td_api::object_ptr<td_api::inputMessageText> get_input_message_text_object(const InputMessageText &input_message_text) {
  td_api::object_ptr<td_api::linkPreviewOptions> options;
  if (!input_message_text.web_page_url.empty() || input_message_text.disable_web_page_preview ||
      input_message_text.force_small_media || input_message_text.force_large_media) {
    options = td_api::make_object<td_api::linkPreviewOptions>(
        input_message_text.disable_web_page_preview, input_message_text.web_page_url,
        input_message_text.force_small_media, input_message_text.force_large_media);
  }
  return td_api::make_object<td_api::inputMessageText>(get_formatted_text_object(input_message_text.text, false, -1),
                                                       std::move(options), input_message_text.clear_draft);
}

}  // namespace td
