//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/StickerPhotoSize.h"
#include "td/telegram/StickerSetId.hpp"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void store(const StickerPhotoSize &sticker_photo_size, StorerT &storer) {
  bool is_custom_emoji = sticker_photo_size.type_ == StickerPhotoSize::Type::CustomEmoji;
  bool is_sticker = sticker_photo_size.type_ == StickerPhotoSize::Type::Sticker;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_custom_emoji);
  STORE_FLAG(is_sticker);
  END_STORE_FLAGS();
  if (is_custom_emoji) {
    store(sticker_photo_size.custom_emoji_id_, storer);
  } else if (is_sticker) {
    store(sticker_photo_size.sticker_set_id_, storer);
    store(sticker_photo_size.sticker_id_, storer);
  }
  store(sticker_photo_size.background_colors_, storer);
}

template <class ParserT>
void parse(StickerPhotoSize &sticker_photo_size, ParserT &parser) {
  bool is_custom_emoji;
  bool is_sticker;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_custom_emoji);
  PARSE_FLAG(is_sticker);
  END_PARSE_FLAGS();
  if (is_custom_emoji) {
    sticker_photo_size.type_ = StickerPhotoSize::Type::CustomEmoji;
    parse(sticker_photo_size.custom_emoji_id_, parser);
  } else if (is_sticker) {
    sticker_photo_size.type_ = StickerPhotoSize::Type::Sticker;
    parse(sticker_photo_size.sticker_set_id_, parser);
    parse(sticker_photo_size.sticker_id_, parser);
  } else {
    UNREACHABLE();
  }
  parse(sticker_photo_size.background_colors_, parser);
}

}  // namespace td
