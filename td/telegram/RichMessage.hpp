//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/RichMessage.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void RichMessage::store(StorerT &storer) const {
  bool has_input_type = input_type_ != InputType::None;
  bool has_source = !source_.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_rtl_);
  STORE_FLAG(is_full_);
  STORE_FLAG(noautolink_);
  STORE_FLAG(has_input_type);
  STORE_FLAG(has_source);
  END_STORE_FLAGS();
  td::store(blocks_, storer);
  if (has_input_type) {
    td::store(input_type_, storer);
  }
  if (has_source) {
    td::store(source_, storer);
  }
}

template <class ParserT>
void RichMessage::parse(ParserT &parser) {
  bool has_input_type;
  bool has_source;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_rtl_);
  PARSE_FLAG(is_full_);
  PARSE_FLAG(noautolink_);
  PARSE_FLAG(has_input_type);
  PARSE_FLAG(has_source);
  END_PARSE_FLAGS();
  td::parse(blocks_, parser);
  if (has_input_type) {
    td::parse(input_type_, parser);
  }
  if (has_source) {
    td::parse(source_, parser);
  }
}

}  // namespace td
