//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ThemeSettings.h"

#include "td/telegram/BackgroundInfo.hpp"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void ThemeSettings::store(StorerT &storer) const {
  bool has_message_accent_color = message_accent_color != accent_color;
  bool has_background = background_info.is_valid();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(animate_message_colors);
  STORE_FLAG(has_message_accent_color);
  STORE_FLAG(has_background);
  END_STORE_FLAGS();
  td::store(accent_color, storer);
  if (has_message_accent_color) {
    td::store(message_accent_color, storer);
  }
  if (has_background) {
    td::store(background_info, storer);
  }
  td::store(base_theme, storer);
  td::store(message_colors, storer);
}

template <class ParserT>
void ThemeSettings::parse(ParserT &parser) {
  bool has_message_accent_color;
  bool has_background;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(animate_message_colors);
  PARSE_FLAG(has_message_accent_color);
  PARSE_FLAG(has_background);
  END_PARSE_FLAGS();
  td::parse(accent_color, parser);
  if (has_message_accent_color) {
    td::parse(message_accent_color, parser);
  } else {
    message_accent_color = accent_color;
  }
  if (has_background) {
    td::parse(background_info, parser);
  }
  td::parse(base_theme, parser);
  td::parse(message_colors, parser);
}

}  // namespace td
