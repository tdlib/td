//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/PeerColorCollectible.h"

#include "td/telegram/BackgroundInfo.hpp"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void PeerColorCollectible::store(StorerT &storer) const {
  bool has_dark_accent_color = dark_accent_color_ != light_accent_color_;
  bool has_dark_colors = dark_colors_ != light_colors_;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_dark_accent_color);
  STORE_FLAG(has_dark_colors);
  END_STORE_FLAGS();
  td::store(unique_gift_id_, storer);
  td::store(gift_custom_emoji_id_, storer);
  td::store(background_custom_emoji_id_, storer);
  td::store(light_accent_color_, storer);
  td::store(light_colors_, storer);
  if (has_dark_accent_color) {
    td::store(dark_accent_color_, storer);
  }
  if (has_dark_colors) {
    td::store(dark_colors_, storer);
  }
}

template <class ParserT>
void PeerColorCollectible::parse(ParserT &parser) {
  bool has_dark_accent_color;
  bool has_dark_colors;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_dark_accent_color);
  PARSE_FLAG(has_dark_colors);
  END_PARSE_FLAGS();
  td::parse(unique_gift_id_, parser);
  td::parse(gift_custom_emoji_id_, parser);
  td::parse(background_custom_emoji_id_, parser);
  td::parse(light_accent_color_, parser);
  td::parse(light_colors_, parser);
  if (has_dark_accent_color) {
    td::parse(dark_accent_color_, parser);
  } else {
    dark_accent_color_ = light_accent_color_;
  }
  if (has_dark_colors) {
    td::parse(dark_colors_, parser);
  } else {
    dark_colors_ = light_colors_;
  }
}

}  // namespace td
