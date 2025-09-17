//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ChatTheme.h"

#include "td/telegram/StarGift.hpp"
#include "td/telegram/ThemeSettings.hpp"
#include "td/telegram/Version.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void ChatTheme::store(StorerT &storer) const {
  bool has_type = type_ != Type::Default;
  bool has_emoji = !emoji_.empty();
  bool has_star_gift = star_gift_.is_valid();
  bool has_light_theme = !light_theme_.is_empty();
  bool has_dark_theme = !dark_theme_.is_empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_type);
  STORE_FLAG(has_emoji);
  STORE_FLAG(has_star_gift);
  STORE_FLAG(has_light_theme);
  STORE_FLAG(has_dark_theme);
  END_STORE_FLAGS();
  if (has_type) {
    td::store(type_, storer);
  }
  if (has_emoji) {
    td::store(emoji_, storer);
  }
  if (has_star_gift) {
    td::store(star_gift_, storer);
  }
  if (has_light_theme) {
    td::store(light_theme_, storer);
  }
  if (has_dark_theme) {
    td::store(dark_theme_, storer);
  }
}

template <class ParserT>
void ChatTheme::parse(ParserT &parser) {
  if (parser.version() < static_cast<int32>(Version::SupportGiftChatThemes)) {
    td::parse(emoji_, parser);
    if (!emoji_.empty()) {
      type_ = Type::Emoji;
    }
    return;
  }
  bool has_type;
  bool has_emoji;
  bool has_star_gift;
  bool has_light_theme;
  bool has_dark_theme;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_type);
  PARSE_FLAG(has_emoji);
  PARSE_FLAG(has_star_gift);
  PARSE_FLAG(has_light_theme);
  PARSE_FLAG(has_dark_theme);
  END_PARSE_FLAGS();
  if (has_type) {
    td::parse(type_, parser);
  }
  if (has_emoji) {
    td::parse(emoji_, parser);
  }
  if (has_star_gift) {
    td::parse(star_gift_, parser);
  }
  if (has_light_theme) {
    td::parse(light_theme_, parser);
  }
  if (has_dark_theme) {
    td::parse(dark_theme_, parser);
  }
}

}  // namespace td
