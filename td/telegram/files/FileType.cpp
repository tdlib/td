//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/files/FileType.h"

namespace td {

FileType get_file_type(const td_api::FileType &file_type) {
  switch (file_type.get_id()) {
    case td_api::fileTypeThumbnail::ID:
      return FileType::Thumbnail;
    case td_api::fileTypeProfilePhoto::ID:
      return FileType::ProfilePhoto;
    case td_api::fileTypePhoto::ID:
      return FileType::Photo;
    case td_api::fileTypeVoiceNote::ID:
      return FileType::VoiceNote;
    case td_api::fileTypeVideo::ID:
      return FileType::Video;
    case td_api::fileTypeDocument::ID:
      return FileType::Document;
    case td_api::fileTypeSecret::ID:
      return FileType::Encrypted;
    case td_api::fileTypeUnknown::ID:
      return FileType::Temp;
    case td_api::fileTypeSticker::ID:
      return FileType::Sticker;
    case td_api::fileTypeAudio::ID:
      return FileType::Audio;
    case td_api::fileTypeAnimation::ID:
      return FileType::Animation;
    case td_api::fileTypeSecretThumbnail::ID:
      return FileType::EncryptedThumbnail;
    case td_api::fileTypeWallpaper::ID:
      return FileType::Background;
    case td_api::fileTypeVideoNote::ID:
      return FileType::VideoNote;
    case td_api::fileTypeSecure::ID:
      return FileType::Secure;
    case td_api::fileTypeNone::ID:
      return FileType::None;
    default:
      UNREACHABLE();
      return FileType::None;
  }
}

tl_object_ptr<td_api::FileType> get_file_type_object(FileType file_type) {
  switch (file_type) {
    case FileType::Thumbnail:
      return make_tl_object<td_api::fileTypeThumbnail>();
    case FileType::ProfilePhoto:
      return make_tl_object<td_api::fileTypeProfilePhoto>();
    case FileType::Photo:
      return make_tl_object<td_api::fileTypePhoto>();
    case FileType::VoiceNote:
      return make_tl_object<td_api::fileTypeVoiceNote>();
    case FileType::Video:
      return make_tl_object<td_api::fileTypeVideo>();
    case FileType::Document:
      return make_tl_object<td_api::fileTypeDocument>();
    case FileType::Encrypted:
      return make_tl_object<td_api::fileTypeSecret>();
    case FileType::Temp:
      return make_tl_object<td_api::fileTypeUnknown>();
    case FileType::Sticker:
      return make_tl_object<td_api::fileTypeSticker>();
    case FileType::Audio:
      return make_tl_object<td_api::fileTypeAudio>();
    case FileType::Animation:
      return make_tl_object<td_api::fileTypeAnimation>();
    case FileType::EncryptedThumbnail:
      return make_tl_object<td_api::fileTypeSecretThumbnail>();
    case FileType::Wallpaper:
      return make_tl_object<td_api::fileTypeWallpaper>();
    case FileType::VideoNote:
      return make_tl_object<td_api::fileTypeVideoNote>();
    case FileType::Secure:
      return make_tl_object<td_api::fileTypeSecure>();
    case FileType::SecureRaw:
      UNREACHABLE();
      return make_tl_object<td_api::fileTypeSecure>();
    case FileType::Background:
      return make_tl_object<td_api::fileTypeWallpaper>();
    case FileType::DocumentAsFile:
      return make_tl_object<td_api::fileTypeDocument>();
    case FileType::None:
      return make_tl_object<td_api::fileTypeNone>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

FileType get_main_file_type(FileType file_type) {
  switch (file_type) {
    case FileType::Wallpaper:
      return FileType::Background;
    case FileType::SecureRaw:
      return FileType::Secure;
    case FileType::DocumentAsFile:
      return FileType::Document;
    default:
      return file_type;
  }
}

CSlice get_file_type_name(FileType file_type) {
  switch (file_type) {
    case FileType::Thumbnail:
      return CSlice("thumbnails");
    case FileType::ProfilePhoto:
      return CSlice("profile_photos");
    case FileType::Photo:
      return CSlice("photos");
    case FileType::VoiceNote:
      return CSlice("voice");
    case FileType::Video:
      return CSlice("videos");
    case FileType::Document:
      return CSlice("documents");
    case FileType::Encrypted:
      return CSlice("secret");
    case FileType::Temp:
      return CSlice("temp");
    case FileType::Sticker:
      return CSlice("stickers");
    case FileType::Audio:
      return CSlice("music");
    case FileType::Animation:
      return CSlice("animations");
    case FileType::EncryptedThumbnail:
      return CSlice("secret_thumbnails");
    case FileType::Wallpaper:
      return CSlice("wallpapers");
    case FileType::VideoNote:
      return CSlice("video_notes");
    case FileType::SecureRaw:
      return CSlice("passport");
    case FileType::Secure:
      return CSlice("passport");
    case FileType::Background:
      return CSlice("wallpapers");
    case FileType::DocumentAsFile:
      return CSlice("documents");
    case FileType::Size:
    case FileType::None:
    default:
      UNREACHABLE();
      return CSlice("none");
  }
}

StringBuilder &operator<<(StringBuilder &string_builder, FileType file_type) {
  return string_builder << get_file_type_name(file_type);
}

FileDirType get_file_dir_type(FileType file_type) {
  switch (file_type) {
    case FileType::Thumbnail:
    case FileType::ProfilePhoto:
    case FileType::Encrypted:
    case FileType::Sticker:
    case FileType::Temp:
    case FileType::Wallpaper:
    case FileType::EncryptedThumbnail:
    case FileType::Secure:
    case FileType::SecureRaw:
    case FileType::Background:
      return FileDirType::Secure;
    default:
      return FileDirType::Common;
  }
}

bool is_file_big(FileType file_type, int64 expected_size) {
  switch (file_type) {
    case FileType::Thumbnail:
    case FileType::ProfilePhoto:
    case FileType::Photo:
    case FileType::EncryptedThumbnail:
      return false;
    default:
      break;
  }

  constexpr int64 SMALL_FILE_MAX_SIZE = 10 << 20;
  return expected_size > SMALL_FILE_MAX_SIZE;
}

}  // namespace td
