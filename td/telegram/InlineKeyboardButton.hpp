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
void store(const InlineKeyboardButton &button, StorerT &storer) {
  bool has_id = button.id != 0;
  bool has_user_id = button.user_id.is_valid();
  bool has_forward_text = !button.forward_text.empty();
  bool has_data = !button.data.empty();
  bool has_style = !button.style.is_default();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_id);
  STORE_FLAG(has_user_id);
  STORE_FLAG(has_forward_text);
  STORE_FLAG(has_data);
  STORE_FLAG(has_style);
  END_STORE_FLAGS();
  store(button.type, storer);
  if (has_id) {
    store(button.id, storer);
  }
  if (has_user_id) {
    store(button.user_id, storer);
  }
  store(button.text, storer);
  if (has_forward_text) {
    store(button.forward_text, storer);
  }
  if (has_data) {
    store(button.data, storer);
  }
  if (has_style) {
    store(button.style, storer);
  }
}

template <class ParserT>
void parse(InlineKeyboardButton &button, ParserT &parser) {
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
    parse(button.type, parser);
    if (has_id) {
      parse(button.id, parser);
    }
    if (has_user_id) {
      parse(button.user_id, parser);
    }
    parse(button.text, parser);
    if (has_forward_text) {
      parse(button.forward_text, parser);
    }
    if (has_data) {
      parse(button.data, parser);
    }
    if (has_style) {
      parse(button.style, parser);
    }
  } else {
    parse(button.type, parser);
    if (button.type == InlineKeyboardButton::Type::UrlAuth) {
      if (parser.version() >= static_cast<int32>(Version::Support64BitIds)) {
        parse(button.id, parser);
      } else {
        int32 old_id;
        parse(old_id, parser);
        button.id = old_id;
      }
    }
    parse(button.text, parser);
    parse(button.data, parser);
  }
}

}  // namespace td
