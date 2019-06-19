//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileType.h"

#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/Variant.h"

namespace td {

struct PhotoSizeSource {
  enum class Type : int32 { Empty, Thumbnail, DialogPhoto, StickerSetThumbnail };

  // for photos, document thumbnails, encrypted thumbnails
  struct Thumbnail {
    Thumbnail() = default;
    Thumbnail(FileType file_type, int32 thumbnail_type) : file_type(file_type), thumbnail_type(thumbnail_type) {
    }

    FileType file_type;
    int32 thumbnail_type = 0;
  };

  // for dialog photos
  struct DialogPhoto {
    DialogPhoto() = default;
    DialogPhoto(DialogId dialog_id, int64 dialog_access_hash, bool is_big)
        : dialog_id(dialog_id), dialog_access_hash(dialog_access_hash), is_big(is_big) {
    }

    tl_object_ptr<telegram_api::InputPeer> get_input_peer() const;

    DialogId dialog_id;
    int64 dialog_access_hash = 0;
    bool is_big = false;
  };

  // for sticker set thumbnails
  struct StickerSetThumbnail {
    int64 sticker_set_id = 0;
    int64 sticker_set_access_hash = 0;

    StickerSetThumbnail() = default;
    StickerSetThumbnail(int64 sticker_set_id, int64 sticker_set_access_hash)
        : sticker_set_id(sticker_set_id), sticker_set_access_hash(sticker_set_access_hash) {
    }

    tl_object_ptr<telegram_api::InputStickerSet> get_input_sticker_set() const {
      return make_tl_object<telegram_api::inputStickerSetID>(sticker_set_id, sticker_set_access_hash);
    }
  };

  PhotoSizeSource() = default;
  PhotoSizeSource(FileType file_type, int32 thumbnail_type) : variant(Thumbnail(file_type, thumbnail_type)) {
  }
  PhotoSizeSource(DialogId dialog_id, int64 dialog_access_hash, bool is_big)
      : variant(DialogPhoto(dialog_id, dialog_access_hash, is_big)) {
  }
  PhotoSizeSource(int64 sticker_set_id, int64 sticker_set_access_hash)
      : variant(StickerSetThumbnail(sticker_set_id, sticker_set_access_hash)) {
  }

  Type get_type() const {
    return static_cast<Type>(variant.get_offset() + 1);
  }

  FileType get_file_type() const;

  Thumbnail &thumbnail() {
    return variant.get<Thumbnail>();
  }
  const Thumbnail &thumbnail() const {
    return variant.get<Thumbnail>();
  }
  const DialogPhoto &dialog_photo() const {
    return variant.get<DialogPhoto>();
  }
  const StickerSetThumbnail &sticker_set_thumbnail() const {
    return variant.get<StickerSetThumbnail>();
  }

  template <class StorerT>
  void store(StorerT &storer) const;
  template <class ParserT>
  void parse(ParserT &parser);

 private:
  Variant<Thumbnail, DialogPhoto, StickerSetThumbnail> variant;
};

bool operator==(const PhotoSizeSource &lhs, const PhotoSizeSource &rhs);
bool operator!=(const PhotoSizeSource &lhs, const PhotoSizeSource &rhs);

}  // namespace td
