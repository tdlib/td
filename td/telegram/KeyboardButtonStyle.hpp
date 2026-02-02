//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/KeyboardButtonStyle.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void KeyboardButtonStyle::store(StorerT &storer) const {
  bool has_type = type_ != Type::Default;
  bool has_icon_custom_emoji_id = icon_custom_emoji_id_.is_valid();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_type);
  STORE_FLAG(has_icon_custom_emoji_id);
  END_STORE_FLAGS();
  if (has_type) {
    td::store(type_, storer);
  }
  if (has_icon_custom_emoji_id) {
    td::store(icon_custom_emoji_id_, storer);
  }
}

template <class ParserT>
void KeyboardButtonStyle::parse(ParserT &parser) {
  bool has_type;
  bool has_icon_custom_emoji_id;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_type);
  PARSE_FLAG(has_icon_custom_emoji_id);
  END_PARSE_FLAGS();
  if (has_type) {
    td::parse(type_, parser);
  }
  if (has_icon_custom_emoji_id) {
    td::parse(icon_custom_emoji_id_, parser);
  }
}

}  // namespace td
