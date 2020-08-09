//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

struct MessageCopyOptions {
  bool send_copy = false;
  bool remove_caption = false;

  MessageCopyOptions() = default;
  MessageCopyOptions(bool send_copy, bool remove_caption): send_copy(send_copy), remove_caption(remove_caption) {
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, MessageCopyOptions copy_options) {
  if (copy_options.send_copy) {
    string_builder << "CopyOptions[remove_caption = " << copy_options.remove_caption << "]";
  }
  return string_builder;
}

}  // namespace td
