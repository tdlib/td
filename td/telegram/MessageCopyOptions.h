//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MessageEntity.h"
#include "td/telegram/ReplyMarkup.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

struct MessageCopyOptions {
  bool send_copy = false;
  bool replace_caption = false;
  FormattedText new_caption;
  unique_ptr<ReplyMarkup> reply_markup;

  MessageCopyOptions() = default;
  MessageCopyOptions(bool send_copy, bool remove_caption) : send_copy(send_copy), replace_caption(remove_caption) {
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, MessageCopyOptions copy_options) {
  if (copy_options.send_copy) {
    string_builder << "CopyOptions[replace_caption = " << copy_options.replace_caption;
  }
  if (copy_options.replace_caption) {
    string_builder << ", new_caption = " << copy_options.new_caption;
  }
  if (copy_options.send_copy) {
    string_builder << "]";
  }
  return string_builder;
}

}  // namespace td
