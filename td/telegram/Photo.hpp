//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/Photo.h"

#include "td/telegram/files/FileId.hpp"

#include "td/utils/logging.h"
#include "td/utils/tl_helpers.h"

#include <limits>

namespace td {

template <class StorerT>
void PhotoSizeSource::StickerSetThumbnail::store(StorerT &storer) const {
  using td::store;
  store(sticker_set_id, storer);
  store(sticker_set_access_hash, storer);
}

template <class ParserT>
void PhotoSizeSource::StickerSetThumbnail::parse(ParserT &parser) {
  using td::parse;
  parse(sticker_set_id, parser);
  parse(sticker_set_access_hash, parser);
}

template <class StorerT>
void PhotoSizeSource::DialogPhoto::store(StorerT &storer) const {
  using td::store;
  store(dialog_id, storer);
  store(dialog_access_hash, storer);
  store(is_big, storer);
}

template <class ParserT>
void PhotoSizeSource::DialogPhoto::parse(ParserT &parser) {
  using td::parse;
  parse(dialog_id, parser);
  parse(dialog_access_hash, parser);
  parse(is_big, parser);

  switch (dialog_id.get_type()) {
    case DialogType::SecretChat:
    case DialogType::None:
      parser.set_error("Invalid chat id");
      break;
    default:
      break;
  }
}

template <class StorerT>
void PhotoSizeSource::store(StorerT &storer) const {
  using td::store;
  store(file_type, storer);
  store(type, storer);
  switch (type) {
    case Type::DialogPhoto:
      store(dialog_photo(), storer);
      break;
    case Type::StickerSetThumbnail:
      store(sticker_set_thumbnail(), storer);
      break;
    case Type::Thumbnail: {
      auto &thumbnail = this->thumbnail();
      store(thumbnail.thumbnail_type, storer);
      break;
    }
    case Type::Empty:
      break;
  }
}

template <class ParserT>
void PhotoSizeSource::parse(ParserT &parser) {
  using td::parse;
  parse(file_type, parser);
  parse(type, parser);
  switch (type) {
    case Type::DialogPhoto: {
      DialogPhoto dialog_photo;
      parse(dialog_photo, parser);
      variant = dialog_photo;
      break;
    }
    case Type::StickerSetThumbnail: {
      StickerSetThumbnail sticker_set_thumbnail;
      parse(sticker_set_thumbnail, parser);
      variant = sticker_set_thumbnail;
      break;
    }
    case Type::Thumbnail: {
      Thumbnail thumbnail;
      parse(thumbnail.thumbnail_type, parser);
      if (thumbnail.thumbnail_type < 0 || thumbnail.thumbnail_type > std::numeric_limits<uint8>::max()) {
        parser.set_error("Wrong thumbnail type");
      }
      variant = thumbnail;
      break;
    }
    case Type::Empty:
      break;
  }
}

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
  store(dialog_photo.small_file_id, storer);
  store(dialog_photo.big_file_id, storer);
}

template <class ParserT>
void parse(DialogPhoto &dialog_photo, ParserT &parser) {
  parse(dialog_photo.small_file_id, parser);
  parse(dialog_photo.big_file_id, parser);
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
void store(const Photo &photo, StorerT &storer) {
  bool has_minithumbnail = !photo.minithumbnail.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(photo.has_stickers);
  STORE_FLAG(has_minithumbnail);
  END_STORE_FLAGS();
  store(photo.id, storer);
  store(photo.date, storer);
  store(photo.photos, storer);
  if (photo.has_stickers) {
    store(photo.sticker_file_ids, storer);
  }
  if (has_minithumbnail) {
    store(photo.minithumbnail, storer);
  }
}

template <class ParserT>
void parse(Photo &photo, ParserT &parser) {
  bool has_minithumbnail;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(photo.has_stickers);
  PARSE_FLAG(has_minithumbnail);
  END_PARSE_FLAGS();
  parse(photo.id, parser);
  parse(photo.date, parser);
  parse(photo.photos, parser);
  if (photo.has_stickers) {
    parse(photo.sticker_file_ids, parser);
  }
  if (has_minithumbnail) {
    parse(photo.minithumbnail, parser);
  }
}

}  // namespace td
