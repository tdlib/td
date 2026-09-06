//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/KeyboardButton.h"
#include "td/telegram/KeyboardButtonStyle.hpp"
#include "td/telegram/RequestedDialogType.hpp"
#include "td/telegram/Version.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void KeyboardButton::store(StorerT &storer) const {
  using td::store;
  bool has_url = !url_.empty();
  bool has_requested_dialog_type = requested_dialog_type_ != nullptr;
  bool has_style = !style_.is_default();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_url);
  STORE_FLAG(has_requested_dialog_type);
  STORE_FLAG(has_style);
  END_STORE_FLAGS();
  store(type_, storer);
  store(text_, storer);
  if (has_url) {
    store(url_, storer);
  }
  if (has_requested_dialog_type) {
    store(requested_dialog_type_, storer);
  }
  if (has_style) {
    store(style_, storer);
  }
}

template <class ParserT>
void KeyboardButton::parse(ParserT &parser) {
  using td::parse;
  bool has_url;
  bool has_requested_dialog_type;
  bool has_style;
  if (parser.version() >= static_cast<int32>(Version::AddKeyboardButtonFlags)) {
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(has_url);
    PARSE_FLAG(has_requested_dialog_type);
    PARSE_FLAG(has_style);
    END_PARSE_FLAGS();
  } else {
    has_url = false;
    has_requested_dialog_type = false;
    has_style = false;
  }
  parse(type_, parser);
  parse(text_, parser);
  if (has_url) {
    parse(url_, parser);
  }
  if (has_requested_dialog_type) {
    parse(requested_dialog_type_, parser);
  }
  if (has_style) {
    parse(style_, parser);
  }
}

}  // namespace td
