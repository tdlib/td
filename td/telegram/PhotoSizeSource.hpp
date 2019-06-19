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
void PhotoSizeSource::Thumbnail::store(StorerT &storer) const {
  using td::store;
  store(file_type, storer);
  store(thumbnail_type, storer);
}

template <class ParserT>
void PhotoSizeSource::Thumbnail::parse(ParserT &parser) {
  using td::parse;
  parse(file_type, parser);
  parse(thumbnail_type, parser);
  if (thumbnail_type < 0 || thumbnail_type > 255) {
    parser.set_error("Wrong thumbnail type");
  }
}

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
  td::store(variant, storer);
}

template <class ParserT>
void PhotoSizeSource::parse(ParserT &parser) {
  td::parse(variant, parser);
}

}  // namespace td
