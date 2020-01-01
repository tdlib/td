//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/InputMessageText.h"

#include "td/telegram/MessageEntity.hpp"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void store(const InputMessageText &input_message_text, StorerT &storer) {
  BEGIN_STORE_FLAGS();
  STORE_FLAG(input_message_text.disable_web_page_preview);
  STORE_FLAG(input_message_text.clear_draft);
  END_STORE_FLAGS();
  store(input_message_text.text, storer);
}

template <class ParserT>
void parse(InputMessageText &input_message_text, ParserT &parser) {
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(input_message_text.disable_web_page_preview);
  PARSE_FLAG(input_message_text.clear_draft);
  END_PARSE_FLAGS();
  parse(input_message_text.text, parser);
}

}  // namespace td
