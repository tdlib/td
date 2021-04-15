//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/PhotoSizeSource.h"

#include "td/telegram/ChannelId.h"
#include "td/telegram/ChatId.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"

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

FileType PhotoSizeSource::get_file_type() const {
  switch (get_type()) {
    case PhotoSizeSource::Type::Thumbnail:
      return thumbnail().file_type;
    case PhotoSizeSource::Type::DialogPhotoSmall:
    case PhotoSizeSource::Type::DialogPhotoBig:
      return FileType::ProfilePhoto;
    case PhotoSizeSource::Type::StickerSetThumbnail:
      return FileType::Thumbnail;
    case PhotoSizeSource::Type::Legacy:
    default:
      UNREACHABLE();
      return FileType::Thumbnail;
  }
}

static bool operator==(const PhotoSizeSource::Legacy &lhs, const PhotoSizeSource::Legacy &rhs) {
  return lhs.secret == rhs.secret;
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

bool operator==(const PhotoSizeSource &lhs, const PhotoSizeSource &rhs) {
  return lhs.variant == rhs.variant;
}

bool operator!=(const PhotoSizeSource &lhs, const PhotoSizeSource &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const PhotoSizeSource &source) {
  switch (source.get_type()) {
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
    case PhotoSizeSource::Type::Legacy:
      return string_builder << "PhotoSizeSourceLegacy[]";
    default:
      UNREACHABLE();
      return string_builder;
  }
}

}  // namespace td
