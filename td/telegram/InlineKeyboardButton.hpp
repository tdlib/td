//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/InlineKeyboardButton.h"
#include "td/telegram/KeyboardButtonStyle.hpp"
#include "td/telegram/Version.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void InlineKeyboardButton::store(StorerT &storer) const {
  using td::store;
  bool has_id = id_ != 0;
  bool has_user_id = user_id_.is_valid();
  bool has_forward_text = !forward_text_.empty();
  bool has_data = !data_.empty();
  bool has_style = !style_.is_default();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_id);
  STORE_FLAG(has_user_id);
  STORE_FLAG(has_forward_text);
  STORE_FLAG(has_data);
  STORE_FLAG(has_style);
  END_STORE_FLAGS();
  store(type_, storer);
  if (has_id) {
    store(id_, storer);
  }
  if (has_user_id) {
    store(user_id_, storer);
  }
  store(text_, storer);
  if (has_forward_text) {
    store(forward_text_, storer);
  }
  if (has_data) {
    store(data_, storer);
  }
  if (has_style) {
    store(style_, storer);
  }
}

template <class ParserT>
void InlineKeyboardButton::parse(ParserT &parser) {
  using td::parse;
  if (parser.version() >= static_cast<int32>(Version::AddKeyboardButtonFlags)) {
    bool has_id;
    bool has_user_id;
    bool has_forward_text;
    bool has_data;
    bool has_style;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(has_id);
    PARSE_FLAG(has_user_id);
    PARSE_FLAG(has_forward_text);
    PARSE_FLAG(has_data);
    PARSE_FLAG(has_style);
    END_PARSE_FLAGS();
    parse(type_, parser);
    if (has_id) {
      parse(id_, parser);
    }
    if (has_user_id) {
      parse(user_id_, parser);
    }
    parse(text_, parser);
    if (has_forward_text) {
      parse(forward_text_, parser);
    }
    if (has_data) {
      parse(data_, parser);
    }
    if (has_style) {
      parse(style_, parser);
    }
  } else {
    parse(type_, parser);
    if (type_ == InlineKeyboardButton::Type::UrlAuth) {
      if (parser.version() >= static_cast<int32>(Version::Support64BitIds)) {
        parse(id_, parser);
      } else {
        int32 old_id;
        parse(old_id, parser);
        id_ = old_id;
      }
    }
    parse(text_, parser);
    parse(data_, parser);
  }
}

}  // namespace td
