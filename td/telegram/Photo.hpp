//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/files/FileId.hpp"
#include "td/telegram/Photo.h"
#include "td/telegram/Version.h"

#include "td/utils/logging.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void store(Dimensions dimensions, StorerT &storer) {
  store(static_cast<uint32>((static_cast<uint32>(dimensions.width) << 16) | dimensions.height), storer);
}

template <class ParserT>
void parse(Dimensions &dimensions, ParserT &parser) {
  uint32 width_height;
  parse(width_height, parser);
  dimensions.width = static_cast<uint16>(width_height >> 16);
  dimensions.height = static_cast<uint16>(width_height & 0xFFFF);
}

template <class StorerT>
void store(const DialogPhoto &dialog_photo, StorerT &storer) {
  bool has_file_ids = dialog_photo.small_file_id.is_valid() || dialog_photo.big_file_id.is_valid();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_file_ids);
  STORE_FLAG(dialog_photo.has_animation);
  END_STORE_FLAGS();
  if (has_file_ids) {
    store(dialog_photo.small_file_id, storer);
    store(dialog_photo.big_file_id, storer);
  }
}

template <class ParserT>
void parse(DialogPhoto &dialog_photo, ParserT &parser) {
  bool has_file_ids = true;
  if (parser.version() >= static_cast<int32>(Version::AddDialogPhotoHasAnimation)) {
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(has_file_ids);
    PARSE_FLAG(dialog_photo.has_animation);
    END_PARSE_FLAGS();
  }
  if (has_file_ids) {
    parse(dialog_photo.small_file_id, parser);
    parse(dialog_photo.big_file_id, parser);
  }
}

template <class StorerT>
void store(const ProfilePhoto &profile_photo, StorerT &storer) {
  store(static_cast<const DialogPhoto &>(profile_photo), storer);
  store(profile_photo.id, storer);
}

template <class ParserT>
void parse(ProfilePhoto &profile_photo, ParserT &parser) {
  parse(static_cast<DialogPhoto &>(profile_photo), parser);
  parse(profile_photo.id, parser);
}

template <class StorerT>
void store(const PhotoSize &photo_size, StorerT &storer) {
  LOG(DEBUG) << "Store photo size " << photo_size;
  store(photo_size.type, storer);
  store(photo_size.dimensions, storer);
  store(photo_size.size, storer);
  store(photo_size.file_id, storer);
}

template <class ParserT>
void parse(PhotoSize &photo_size, ParserT &parser) {
  parse(photo_size.type, parser);
  parse(photo_size.dimensions, parser);
  parse(photo_size.size, parser);
  parse(photo_size.file_id, parser);
  LOG(DEBUG) << "Parsed photo size " << photo_size;
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
void store(const Photo &photo, StorerT &storer) {
  bool has_minithumbnail = !photo.minithumbnail.empty();
  bool has_animations = !photo.animations.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(photo.has_stickers);
  STORE_FLAG(has_minithumbnail);
  STORE_FLAG(has_animations);
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
}

template <class ParserT>
void parse(Photo &photo, ParserT &parser) {
  bool has_minithumbnail;
  bool has_animations;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(photo.has_stickers);
  PARSE_FLAG(has_minithumbnail);
  PARSE_FLAG(has_animations);
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
}

}  // namespace td
