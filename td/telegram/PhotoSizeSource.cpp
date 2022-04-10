//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/PhotoSizeSource.h"

#include "td/telegram/ChannelId.h"
#include "td/telegram/ChatId.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/StackAllocator.h"
#include "td/utils/tl_helpers.h"
#include "td/utils/tl_storers.h"

namespace td {

tl_object_ptr<telegram_api::InputPeer> PhotoSizeSource::DialogPhoto::get_input_peer() const {
  switch (dialog_id.get_type()) {
    case DialogType::User: {
      UserId user_id = dialog_id.get_user_id();
      return make_tl_object<telegram_api::inputPeerUser>(user_id.get(), dialog_access_hash);
    }
    case DialogType::Chat: {
      ChatId chat_id = dialog_id.get_chat_id();
      return make_tl_object<telegram_api::inputPeerChat>(chat_id.get());
    }
    case DialogType::Channel: {
      ChannelId channel_id = dialog_id.get_channel_id();
      return make_tl_object<telegram_api::inputPeerChannel>(channel_id.get(), dialog_access_hash);
    }
    case DialogType::SecretChat:
      return nullptr;
    case DialogType::None:
      return make_tl_object<telegram_api::inputPeerEmpty>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

FileType PhotoSizeSource::get_file_type(const char *source) const {
  switch (get_type(source)) {
    case Type::Thumbnail:
      return thumbnail().file_type;
    case Type::DialogPhotoSmall:
    case Type::DialogPhotoBig:
    case Type::DialogPhotoSmallLegacy:
    case Type::DialogPhotoBigLegacy:
      return FileType::ProfilePhoto;
    case Type::StickerSetThumbnail:
    case Type::StickerSetThumbnailLegacy:
    case Type::StickerSetThumbnailVersion:
      return FileType::Thumbnail;
    case Type::Legacy:
    case Type::FullLegacy:
    default:
      UNREACHABLE();
      return FileType::Thumbnail;
  }
}

string PhotoSizeSource::get_unique() const {
  auto ptr = StackAllocator::alloc(16);
  MutableSlice data = ptr.as_slice();
  TlStorerUnsafe storer(data.ubegin());
  switch (get_type("get_unique")) {
    case Type::Legacy:
      UNREACHABLE();
      break;
    case Type::Thumbnail: {
      auto type = thumbnail().thumbnail_type;
      CHECK(0 <= type && type <= 127);
      if (type == 'a') {
        type = 0;
      } else if (type == 'c') {
        type = 1;
      } else {
        type += 5;
      }
      return string(1, static_cast<char>(type));
    }
    case Type::DialogPhotoSmall:
      // it doesn't matter to which Dialog the photo belongs
      return string(1, '\x00');
    case Type::DialogPhotoBig:
      // it doesn't matter to which Dialog the photo belongs
      return string(1, '\x01');
    case Type::StickerSetThumbnail:
      UNREACHABLE();
      break;
    case Type::FullLegacy: {
      auto &legacy = full_legacy();
      td::store(legacy.volume_id, storer);
      td::store(legacy.local_id, storer);
      break;
    }
    case Type::DialogPhotoSmallLegacy:
    case Type::DialogPhotoBigLegacy: {
      auto &legacy = dialog_photo_legacy();
      td::store(legacy.volume_id, storer);
      td::store(legacy.local_id, storer);
      break;
    }
    case Type::StickerSetThumbnailLegacy: {
      auto &legacy = sticker_set_thumbnail_legacy();
      td::store(legacy.volume_id, storer);
      td::store(legacy.local_id, storer);
      break;
    }
    case Type::StickerSetThumbnailVersion: {
      auto &thumbnail = sticker_set_thumbnail_version();
      storer.store_slice(Slice("\x02"));
      td::store(thumbnail.sticker_set_id, storer);
      td::store(thumbnail.version, storer);
      break;
    }
    default:
      UNREACHABLE();
      break;
  }
  auto size = storer.get_buf() - data.ubegin();
  CHECK(size <= 13);
  return string(data.begin(), size);
}

string PhotoSizeSource::get_unique_name(int64 photo_id) const {
  switch (get_type("get_unique_name")) {
    case Type::Thumbnail:
      CHECK(0 <= thumbnail().thumbnail_type && thumbnail().thumbnail_type <= 127);
      return PSTRING() << photo_id << '_' << thumbnail().thumbnail_type;
    case Type::DialogPhotoSmall:
      return to_string(photo_id);
    case Type::DialogPhotoBig:
      return PSTRING() << photo_id << '_' << 1;
    case Type::StickerSetThumbnailVersion:
      return PSTRING() << sticker_set_thumbnail_version().sticker_set_id << '_'
                       << static_cast<uint32>(sticker_set_thumbnail_version().version);
    case Type::Legacy:
    case Type::StickerSetThumbnail:
    case Type::FullLegacy:
    case Type::DialogPhotoSmallLegacy:
    case Type::DialogPhotoBigLegacy:
    case Type::StickerSetThumbnailLegacy:
    default:
      UNREACHABLE();
      break;
  }
  return string();
}

static bool operator==(const PhotoSizeSource::Legacy &lhs, const PhotoSizeSource::Legacy &rhs) {
  UNREACHABLE();
  return false;
}

static bool operator==(const PhotoSizeSource::Thumbnail &lhs, const PhotoSizeSource::Thumbnail &rhs) {
  return lhs.file_type == rhs.file_type && lhs.thumbnail_type == rhs.thumbnail_type;
}

static bool operator==(const PhotoSizeSource::DialogPhoto &lhs, const PhotoSizeSource::DialogPhoto &rhs) {
  return lhs.dialog_id == rhs.dialog_id && lhs.dialog_access_hash == rhs.dialog_access_hash;
}

static bool operator==(const PhotoSizeSource::DialogPhotoSmall &lhs, const PhotoSizeSource::DialogPhotoSmall &rhs) {
  return static_cast<const PhotoSizeSource::DialogPhoto &>(lhs) ==
         static_cast<const PhotoSizeSource::DialogPhoto &>(rhs);
}

static bool operator==(const PhotoSizeSource::DialogPhotoBig &lhs, const PhotoSizeSource::DialogPhotoBig &rhs) {
  return static_cast<const PhotoSizeSource::DialogPhoto &>(lhs) ==
         static_cast<const PhotoSizeSource::DialogPhoto &>(rhs);
}

static bool operator==(const PhotoSizeSource::StickerSetThumbnail &lhs,
                       const PhotoSizeSource::StickerSetThumbnail &rhs) {
  return lhs.sticker_set_id == rhs.sticker_set_id && lhs.sticker_set_access_hash == rhs.sticker_set_access_hash;
}

static bool operator==(const PhotoSizeSource::FullLegacy &lhs, const PhotoSizeSource::FullLegacy &rhs) {
  return lhs.volume_id == rhs.volume_id && lhs.local_id == rhs.local_id && lhs.secret == rhs.secret;
}

static bool operator==(const PhotoSizeSource::DialogPhotoLegacy &lhs, const PhotoSizeSource::DialogPhotoLegacy &rhs) {
  return static_cast<const PhotoSizeSource::DialogPhoto &>(lhs) ==
             static_cast<const PhotoSizeSource::DialogPhoto &>(rhs) &&
         lhs.volume_id == rhs.volume_id && lhs.local_id == rhs.local_id;
}

static bool operator==(const PhotoSizeSource::DialogPhotoSmallLegacy &lhs,
                       const PhotoSizeSource::DialogPhotoSmallLegacy &rhs) {
  return static_cast<const PhotoSizeSource::DialogPhotoLegacy &>(lhs) ==
         static_cast<const PhotoSizeSource::DialogPhotoLegacy &>(rhs);
}

static bool operator==(const PhotoSizeSource::DialogPhotoBigLegacy &lhs,
                       const PhotoSizeSource::DialogPhotoBigLegacy &rhs) {
  return static_cast<const PhotoSizeSource::DialogPhotoLegacy &>(lhs) ==
         static_cast<const PhotoSizeSource::DialogPhotoLegacy &>(rhs);
}

static bool operator==(const PhotoSizeSource::StickerSetThumbnailLegacy &lhs,
                       const PhotoSizeSource::StickerSetThumbnailLegacy &rhs) {
  return static_cast<const PhotoSizeSource::StickerSetThumbnail &>(lhs) ==
             static_cast<const PhotoSizeSource::StickerSetThumbnail &>(rhs) &&
         lhs.volume_id == rhs.volume_id && lhs.local_id == rhs.local_id;
}

static bool operator==(const PhotoSizeSource::StickerSetThumbnailVersion &lhs,
                       const PhotoSizeSource::StickerSetThumbnailVersion &rhs) {
  return static_cast<const PhotoSizeSource::StickerSetThumbnail &>(lhs) ==
             static_cast<const PhotoSizeSource::StickerSetThumbnail &>(rhs) &&
         lhs.version == rhs.version;
}

bool operator==(const PhotoSizeSource &lhs, const PhotoSizeSource &rhs) {
  return lhs.variant_ == rhs.variant_;
}

bool operator!=(const PhotoSizeSource &lhs, const PhotoSizeSource &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const PhotoSizeSource &source) {
  switch (source.get_type("operator<<")) {
    case PhotoSizeSource::Type::Legacy:
      return string_builder << "PhotoSizeSourceLegacy[]";
    case PhotoSizeSource::Type::Thumbnail:
      return string_builder << "PhotoSizeSourceThumbnail[" << source.thumbnail().file_type
                            << ", type = " << source.thumbnail().thumbnail_type << ']';
    case PhotoSizeSource::Type::DialogPhotoSmall:
      return string_builder << "PhotoSizeSourceChatPhotoSmall[" << source.dialog_photo().dialog_id << ']';
    case PhotoSizeSource::Type::DialogPhotoBig:
      return string_builder << "PhotoSizeSourceChatPhotoBig[" << source.dialog_photo().dialog_id << ']';
    case PhotoSizeSource::Type::StickerSetThumbnail:
      return string_builder << "PhotoSizeSourceStickerSetThumbnail[" << source.sticker_set_thumbnail().sticker_set_id
                            << ']';
    case PhotoSizeSource::Type::FullLegacy:
      return string_builder << "PhotoSizeSourceFullLegacy[]";
    case PhotoSizeSource::Type::DialogPhotoSmallLegacy:
      return string_builder << "PhotoSizeSourceChatPhotoSmallLegacy[" << source.dialog_photo().dialog_id << ']';
    case PhotoSizeSource::Type::DialogPhotoBigLegacy:
      return string_builder << "PhotoSizeSourceChatPhotoBigLegacy[" << source.dialog_photo().dialog_id << ']';
    case PhotoSizeSource::Type::StickerSetThumbnailLegacy:
      return string_builder << "PhotoSizeSourceStickerSetThumbnailLegacy["
                            << source.sticker_set_thumbnail().sticker_set_id << ']';
    case PhotoSizeSource::Type::StickerSetThumbnailVersion:
      return string_builder << "PhotoSizeSourceStickerSetThumbnailVersion["
                            << source.sticker_set_thumbnail().sticker_set_id << '_'
                            << source.sticker_set_thumbnail_version().version << ']';
    default:
      UNREACHABLE();
      return string_builder;
  }
}

}  // namespace td
