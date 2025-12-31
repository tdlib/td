//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageCopyOptions.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ReplyMarkup.h"
#include "td/telegram/Td.h"

namespace td {

MessageCopyOptions::~MessageCopyOptions() = default;

bool MessageCopyOptions::is_supported_server_side(const Td *td, const MessageTopic &message_topic) const {
  if (!send_copy) {
    return true;
  }
  if ((replace_caption && !new_caption.text.empty()) || reply_markup != nullptr) {
    return false;
  }
  if (input_reply_to.is_valid() &&
      (!message_topic.is_forum() || input_reply_to.has_quote() || input_reply_to.has_todo_item_id() ||
       input_reply_to.get_same_chat_reply_to_message_id() != message_topic.get_implicit_reply_to_message_id(td))) {
    return false;
  }
  return true;
}

Result<MessageCopyOptions> MessageCopyOptions::get_message_copy_options(
    Td *td, DialogId dialog_id, td_api::object_ptr<td_api::messageCopyOptions> &&options) {
  if (options == nullptr || !options->send_copy_) {
    return MessageCopyOptions();
  }
  MessageCopyOptions result;
  result.send_copy = true;
  result.replace_caption = options->replace_caption_;
  if (result.replace_caption) {
    TRY_RESULT_ASSIGN(result.new_caption, get_formatted_text(td, dialog_id, std::move(options->new_caption_),
                                                             td->auth_manager_->is_bot(), true, false, false));
    result.new_invert_media = options->new_show_caption_above_media_;
  }
  return std::move(result);
}

StringBuilder &operator<<(StringBuilder &string_builder, MessageCopyOptions copy_options) {
  if (copy_options.send_copy) {
    string_builder << "CopyOptions[replace_caption = " << copy_options.replace_caption;
    if (copy_options.replace_caption) {
      string_builder << ", new_caption = " << copy_options.new_caption
                     << ", new_show_caption_above_media = " << copy_options.new_invert_media;
    }
    if (copy_options.input_reply_to.is_valid()) {
      string_builder << ", in reply to " << copy_options.input_reply_to;
    }
    if (copy_options.reply_markup != nullptr) {
      string_builder << ", with reply markup";
    }
    string_builder << "]";
  }
  return string_builder;
}

}  // namespace td
