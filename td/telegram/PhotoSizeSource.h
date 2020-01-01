//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileType.h"

#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/Variant.h"

namespace td {

struct PhotoSizeSource {
  enum class Type : int32 { Legacy, Thumbnail, DialogPhotoSmall, DialogPhotoBig, StickerSetThumbnail };

  // for legacy photos with secret
  struct Legacy {
    Legacy() = default;
    explicit Legacy(int64 secret) : secret(secret) {
    }

    int64 secret = 0;
  };

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
    DialogPhoto(DialogId dialog_id, int64 dialog_access_hash)
        : dialog_id(dialog_id), dialog_access_hash(dialog_access_hash) {
    }

    tl_object_ptr<telegram_api::InputPeer> get_input_peer() const;

    DialogId dialog_id;
    int64 dialog_access_hash = 0;
  };

  struct DialogPhotoSmall : public DialogPhoto {
    using DialogPhoto::DialogPhoto;
  };

  struct DialogPhotoBig : public DialogPhoto {
    using DialogPhoto::DialogPhoto;
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
  explicit PhotoSizeSource(int64 secret) : variant(Legacy(secret)) {
  }
  PhotoSizeSource(FileType file_type, int32 thumbnail_type) : variant(Thumbnail(file_type, thumbnail_type)) {
  }
  PhotoSizeSource(DialogId dialog_id, int64 dialog_access_hash, bool is_big) {
    if (is_big) {
      variant = DialogPhotoBig(dialog_id, dialog_access_hash);
    } else {
      variant = DialogPhotoSmall(dialog_id, dialog_access_hash);
    }
  }
  PhotoSizeSource(int64 sticker_set_id, int64 sticker_set_access_hash)
      : variant(StickerSetThumbnail(sticker_set_id, sticker_set_access_hash)) {
  }

  Type get_type() const {
    auto offset = variant.get_offset();
    CHECK(offset >= 0);
    return static_cast<Type>(offset);
  }

  FileType get_file_type() const;

  Thumbnail &thumbnail() {
    return variant.get<Thumbnail>();
  }

  const Legacy &legacy() const {
    return variant.get<Legacy>();
  }
  const Thumbnail &thumbnail() const {
    return variant.get<Thumbnail>();
  }
  const DialogPhoto &dialog_photo() const {
    if (variant.get_offset() == 2) {
      return variant.get<DialogPhotoSmall>();
    } else {
      return variant.get<DialogPhotoBig>();
    }
  }
  const StickerSetThumbnail &sticker_set_thumbnail() const {
    return variant.get<StickerSetThumbnail>();
  }

  template <class StorerT>
  void store(StorerT &storer) const;
  template <class ParserT>
  void parse(ParserT &parser);

 private:
  Variant<Legacy, Thumbnail, DialogPhotoSmall, DialogPhotoBig, StickerSetThumbnail> variant;
};

bool operator==(const PhotoSizeSource &lhs, const PhotoSizeSource &rhs);
bool operator!=(const PhotoSizeSource &lhs, const PhotoSizeSource &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const PhotoSizeSource &source);

}  // namespace td
