//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessageInputReplyTo.h"
#include "td/telegram/MessageTopic.h"
#include "td/telegram/td_api.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

namespace td {

struct ReplyMarkup;
class Td;

struct MessageCopyOptions {
  bool send_copy = false;
  bool replace_caption = false;
  bool new_invert_media = false;
  FormattedText new_caption;
  MessageInputReplyTo input_reply_to;
  unique_ptr<ReplyMarkup> reply_markup;

  MessageCopyOptions() = default;
  MessageCopyOptions(bool send_copy, bool remove_caption) : send_copy(send_copy), replace_caption(remove_caption) {
  }
  MessageCopyOptions(const MessageCopyOptions &) = delete;
  MessageCopyOptions &operator=(const MessageCopyOptions &) = delete;
  MessageCopyOptions(MessageCopyOptions &&) noexcept = default;
  MessageCopyOptions &operator=(MessageCopyOptions &&) noexcept = default;
  ~MessageCopyOptions();

  static Result<MessageCopyOptions> get_message_copy_options(Td *td, DialogId dialog_id,
                                                             td_api::object_ptr<td_api::messageCopyOptions> &&options);

  bool is_supported_server_side(const Td *td, const MessageTopic &message_topic) const;
};

StringBuilder &operator<<(StringBuilder &string_builder, MessageCopyOptions copy_options);

}  // namespace td
