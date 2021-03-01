//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileType.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/PhotoSizeSource.h"
#include "td/telegram/SecretInputMedia.h"
#include "td/telegram/UserId.h"

#include "td/telegram/secret_api.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/MovableValue.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/Variant.h"

namespace td {

class FileManager;

struct Dimensions {
  uint16 width = 0;
  uint16 height = 0;
};

struct DialogPhoto {
  FileId small_file_id;
  FileId big_file_id;
  bool has_animation = false;
};

struct ProfilePhoto : public DialogPhoto {
  int64 id = 0;
};

struct PhotoSize {
  int32 type = 0;
  Dimensions dimensions;
  int32 size = 0;
  FileId file_id;
  vector<int32> progressive_sizes;
};

struct AnimationSize : public PhotoSize {
  double main_frame_timestamp = 0.0;
};

struct Photo {
  MovableValue<int64, -2> id;
  int32 date = 0;
  string minithumbnail;
  vector<PhotoSize> photos;

  vector<AnimationSize> animations;

  bool has_stickers = false;
  vector<FileId> sticker_file_ids;

  bool is_empty() const {
    return id.get() == -2;
  }
};

Dimensions get_dimensions(int32 width, int32 height, const char *source);

bool operator==(const Dimensions &lhs, const Dimensions &rhs);
bool operator!=(const Dimensions &lhs, const Dimensions &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const Dimensions &dimensions);

td_api::object_ptr<td_api::minithumbnail> get_minithumbnail_object(const string &packed);

ProfilePhoto get_profile_photo(FileManager *file_manager, UserId user_id, int64 user_access_hash,
                               tl_object_ptr<telegram_api::UserProfilePhoto> &&profile_photo_ptr);
tl_object_ptr<td_api::profilePhoto> get_profile_photo_object(FileManager *file_manager,
                                                             const ProfilePhoto &profile_photo);

bool operator==(const ProfilePhoto &lhs, const ProfilePhoto &rhs);
bool operator!=(const ProfilePhoto &lhs, const ProfilePhoto &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const ProfilePhoto &profile_photo);

DialogPhoto get_dialog_photo(FileManager *file_manager, DialogId dialog_id, int64 dialog_access_hash,
                             tl_object_ptr<telegram_api::ChatPhoto> &&chat_photo_ptr);
tl_object_ptr<td_api::chatPhotoInfo> get_chat_photo_info_object(FileManager *file_manager,
                                                                const DialogPhoto *dialog_photo);

DialogPhoto as_fake_dialog_photo(const Photo &photo);

ProfilePhoto as_profile_photo(FileManager *file_manager, UserId user_id, int64 user_access_hash, const Photo &photo);

vector<FileId> dialog_photo_get_file_ids(const DialogPhoto &dialog_photo);

bool operator==(const DialogPhoto &lhs, const DialogPhoto &rhs);
bool operator!=(const DialogPhoto &lhs, const DialogPhoto &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const DialogPhoto &dialog_photo);

enum class PhotoFormat : int32 { Jpeg, Png, Webp, Gif, Tgs, Mpeg4 };

PhotoSize get_secret_thumbnail_photo_size(FileManager *file_manager, BufferSlice bytes, DialogId owner_dialog_id,
                                          int32 width, int32 height);
Variant<PhotoSize, string> get_photo_size(FileManager *file_manager, PhotoSizeSource source, int64 id,
                                          int64 access_hash, string file_reference, DcId dc_id,
                                          DialogId owner_dialog_id, tl_object_ptr<telegram_api::PhotoSize> &&size_ptr,
                                          PhotoFormat format);
AnimationSize get_animation_size(FileManager *file_manager, PhotoSizeSource source, int64 id, int64 access_hash,
                                 string file_reference, DcId dc_id, DialogId owner_dialog_id,
                                 tl_object_ptr<telegram_api::videoSize> &&size);
PhotoSize get_web_document_photo_size(FileManager *file_manager, FileType file_type, DialogId owner_dialog_id,
                                      tl_object_ptr<telegram_api::WebDocument> web_document_ptr);
td_api::object_ptr<td_api::thumbnail> get_thumbnail_object(FileManager *file_manager, const PhotoSize &photo_size,
                                                           PhotoFormat format);

bool operator==(const PhotoSize &lhs, const PhotoSize &rhs);
bool operator!=(const PhotoSize &lhs, const PhotoSize &rhs);

bool operator<(const PhotoSize &lhs, const PhotoSize &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const PhotoSize &photo_size);

bool operator==(const AnimationSize &lhs, const AnimationSize &rhs);
bool operator!=(const AnimationSize &lhs, const AnimationSize &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const AnimationSize &animation_size);

Photo get_photo(FileManager *file_manager, tl_object_ptr<telegram_api::Photo> &&photo, DialogId owner_dialog_id);
Photo get_photo(FileManager *file_manager, tl_object_ptr<telegram_api::photo> &&photo, DialogId owner_dialog_id);
Photo get_encrypted_file_photo(FileManager *file_manager, tl_object_ptr<telegram_api::encryptedFile> &&file,
                               tl_object_ptr<secret_api::decryptedMessageMediaPhoto> &&photo, DialogId owner_dialog_id);
Photo get_web_document_photo(FileManager *file_manager, tl_object_ptr<telegram_api::WebDocument> web_document,
                             DialogId owner_dialog_id);
tl_object_ptr<td_api::photo> get_photo_object(FileManager *file_manager, const Photo &photo);
tl_object_ptr<td_api::chatPhoto> get_chat_photo_object(FileManager *file_manager, const Photo &photo);

void photo_delete_thumbnail(Photo &photo);

bool photo_has_input_media(FileManager *file_manager, const Photo &photo, bool is_secret, bool is_bot);

SecretInputMedia photo_get_secret_input_media(FileManager *file_manager, const Photo &photo,
                                              tl_object_ptr<telegram_api::InputEncryptedFile> input_file,
                                              const string &caption, BufferSlice thumbnail);

tl_object_ptr<telegram_api::InputMedia> photo_get_input_media(FileManager *file_manager, const Photo &photo,
                                                              tl_object_ptr<telegram_api::InputFile> input_file,
                                                              int32 ttl);

vector<FileId> photo_get_file_ids(const Photo &photo);

bool operator==(const Photo &lhs, const Photo &rhs);
bool operator!=(const Photo &lhs, const Photo &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const Photo &photo);

tl_object_ptr<telegram_api::userProfilePhoto> convert_photo_to_profile_photo(
    const tl_object_ptr<telegram_api::photo> &photo);

}  // namespace td
