//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/Dimensions.hpp"
#include "td/telegram/files/FileId.hpp"
#include "td/telegram/PhotoSize.h"
#include "td/telegram/StickerSetId.hpp"
#include "td/telegram/Version.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void store(const PhotoSize &photo_size, StorerT &storer) {
  store(photo_size.type, storer);
  store(photo_size.dimensions, storer);
  store(photo_size.size, storer);
  store(photo_size.file_id, storer);
  store(photo_size.progressive_sizes, storer);
}

template <class ParserT>
void parse(PhotoSize &photo_size, ParserT &parser) {
  parse(photo_size.type, parser);
  parse(photo_size.dimensions, parser);
  parse(photo_size.size, parser);
  parse(photo_size.file_id, parser);
  if (parser.version() >= static_cast<int32>(Version::AddPhotoProgressiveSizes)) {
    parse(photo_size.progressive_sizes, parser);
  } else {
    photo_size.progressive_sizes.clear();
  }
  if (photo_size.type < 0 || photo_size.type >= 128) {
    parser.set_error("Wrong PhotoSize type");
    return;
  }
}

template <class StorerT>
void store(const AnimationSize &animation_size, StorerT &storer) {
  store(static_cast<const PhotoSize &>(animation_size), storer);
  store(animation_size.main_frame_timestamp, storer);
}

template <class ParserT>
void parse(AnimationSize &animation_size, ParserT &parser) {
  parse(static_cast<PhotoSize &>(animation_size), parser);
  if (parser.version() >= static_cast<int32>(Version::AddDialogPhotoHasAnimation)) {
    parse(animation_size.main_frame_timestamp, parser);
  } else {
    animation_size.main_frame_timestamp = 0;
  }
}

template <class StorerT>
void store(const StickerPhotoSize &sticker_photo_size, StorerT &storer) {
  bool is_custom_emoji = sticker_photo_size.type == StickerPhotoSize::Type::CustomEmoji;
  bool is_sticker = sticker_photo_size.type == StickerPhotoSize::Type::Sticker;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_custom_emoji);
  STORE_FLAG(is_sticker);
  END_STORE_FLAGS();
  if (is_custom_emoji) {
    store(sticker_photo_size.custom_emoji_id, storer);
  } else if (is_sticker) {
    store(sticker_photo_size.sticker_set_id, storer);
    store(sticker_photo_size.sticker_id, storer);
  }
  store(sticker_photo_size.background_colors, storer);
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
    sticker_photo_size.type = StickerPhotoSize::Type::CustomEmoji;
    parse(sticker_photo_size.custom_emoji_id, parser);
  } else if (is_sticker) {
    sticker_photo_size.type = StickerPhotoSize::Type::Sticker;
    parse(sticker_photo_size.sticker_set_id, parser);
    parse(sticker_photo_size.sticker_id, parser);
  } else {
    UNREACHABLE();
  }
  parse(sticker_photo_size.background_colors, parser);
}

}  // namespace td
