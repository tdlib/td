//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/files/FileId.hpp"
#include "td/telegram/Photo.h"
#include "td/telegram/PhotoSize.hpp"
#include "td/telegram/StickerPhotoSize.hpp"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void store(const Photo &photo, StorerT &storer) {
  bool has_minithumbnail = !photo.minithumbnail.empty();
  bool has_animations = !photo.animations.empty();
  bool has_sticker_photo_size = photo.sticker_photo_size != nullptr;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(photo.has_stickers);
  STORE_FLAG(has_minithumbnail);
  STORE_FLAG(has_animations);
  STORE_FLAG(has_sticker_photo_size);
  END_STORE_FLAGS();
  store(photo.id.get(), storer);
  store(photo.date, storer);
  store(photo.photos, storer);
  if (photo.has_stickers) {
    store(photo.sticker_file_ids, storer);
  }
  if (has_minithumbnail) {
    store(photo.minithumbnail, storer);
  }
  if (has_animations) {
    store(photo.animations, storer);
  }
  if (has_sticker_photo_size) {
    store(photo.sticker_photo_size, storer);
  }
}

template <class ParserT>
void parse(Photo &photo, ParserT &parser) {
  bool has_minithumbnail;
  bool has_animations;
  bool has_sticker_photo_size;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(photo.has_stickers);
  PARSE_FLAG(has_minithumbnail);
  PARSE_FLAG(has_animations);
  PARSE_FLAG(has_sticker_photo_size);
  END_PARSE_FLAGS();
  int64 id;
  parse(id, parser);
  photo.id = id;
  parse(photo.date, parser);
  parse(photo.photos, parser);
  if (photo.has_stickers) {
    parse(photo.sticker_file_ids, parser);
  }
  if (has_minithumbnail) {
    parse(photo.minithumbnail, parser);
  }
  if (has_animations) {
    parse(photo.animations, parser);
  }
  if (has_sticker_photo_size) {
    parse(photo.sticker_photo_size, parser);
  }
}

}  // namespace td
