//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ProfileShareTheme.h"

#include "td/telegram/StarGift.hpp"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void ProfileShareTheme::ThemeEmoji::store(StorerT &storer) const {
  bool has_emoji = !emoji_.empty();
  bool has_custom_emoji_id = custom_emoji_id_.is_valid();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_emoji);
  STORE_FLAG(has_custom_emoji_id);
  END_STORE_FLAGS();
  if (has_emoji) {
    td::store(emoji_, storer);
  }
  if (has_custom_emoji_id) {
    td::store(custom_emoji_id_, storer);
  }
}

template <class ParserT>
void ProfileShareTheme::ThemeEmoji::parse(ParserT &parser) {
  bool has_emoji;
  bool has_custom_emoji_id;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_emoji);
  PARSE_FLAG(has_custom_emoji_id);
  END_PARSE_FLAGS();
  if (has_emoji) {
    td::parse(emoji_, parser);
  }
  if (has_custom_emoji_id) {
    td::parse(custom_emoji_id_, parser);
  }
}

template <class StorerT>
void ProfileShareTheme::store(StorerT &storer) const {
  bool has_type = type_ != Type::Default;
  bool has_emojis = !emojis_.empty();
  bool has_backdrop_id = backdrop_id_ != 0;
  bool has_star_gift = star_gift_.is_valid();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_type);
  STORE_FLAG(has_emojis);
  STORE_FLAG(use_default_background_);
  STORE_FLAG(has_backdrop_id);
  STORE_FLAG(has_star_gift);
  END_STORE_FLAGS();
  if (has_type) {
    td::store(type_, storer);
  }
  if (has_emojis) {
    td::store(emojis_, storer);
  }
  if (has_backdrop_id) {
    td::store(backdrop_id_, storer);
  }
  if (has_star_gift) {
    td::store(star_gift_, storer);
  }
}

template <class ParserT>
void ProfileShareTheme::parse(ParserT &parser) {
  bool has_type;
  bool has_emojis;
  bool has_backdrop_id;
  bool has_star_gift;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_type);
  PARSE_FLAG(has_emojis);
  PARSE_FLAG(use_default_background_);
  PARSE_FLAG(has_backdrop_id);
  PARSE_FLAG(has_star_gift);
  END_PARSE_FLAGS();
  if (has_type) {
    td::parse(type_, parser);
  }
  if (has_emojis) {
    td::parse(emojis_, parser);
  }
  if (has_backdrop_id) {
    td::parse(backdrop_id_, parser);
  }
  if (has_star_gift) {
    td::parse(star_gift_, parser);
  }
}

}  // namespace td
