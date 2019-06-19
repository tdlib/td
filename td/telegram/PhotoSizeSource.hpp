//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/Photo.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void store(const PhotoSizeSource::Thumbnail &source, StorerT &storer) {
  store(source.file_type, storer);
  store(source.thumbnail_type, storer);
}

template <class ParserT>
void parse(PhotoSizeSource::Thumbnail &source, ParserT &parser) {
  parse(source.file_type, parser);
  parse(source.thumbnail_type, parser);
  if (source.thumbnail_type < 0 || source.thumbnail_type > 255) {
    parser.set_error("Wrong thumbnail type");
  }
}

template <class StorerT>
void store(const PhotoSizeSource::StickerSetThumbnail &source, StorerT &storer) {
  store(source.sticker_set_id, storer);
  store(source.sticker_set_access_hash, storer);
}

template <class ParserT>
void parse(PhotoSizeSource::StickerSetThumbnail &source, ParserT &parser) {
  parse(source.sticker_set_id, parser);
  parse(source.sticker_set_access_hash, parser);
}

template <class StorerT>
void store(const PhotoSizeSource::DialogPhoto &source, StorerT &storer) {
  store(source.dialog_id, storer);
  store(source.dialog_access_hash, storer);
  store(source.is_big, storer);
}

template <class ParserT>
void parse(PhotoSizeSource::DialogPhoto &source, ParserT &parser) {
  parse(source.dialog_id, parser);
  parse(source.dialog_access_hash, parser);
  parse(source.is_big, parser);

  switch (source.dialog_id.get_type()) {
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
  td::store(variant, storer);
}

template <class ParserT>
void PhotoSizeSource::parse(ParserT &parser) {
  td::parse(variant, parser);
}

}  // namespace td
