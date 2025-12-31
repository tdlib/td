//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/EncryptedFile.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileType.h"
#include "td/telegram/PhotoSize.h"
#include "td/telegram/secret_api.h"
#include "td/telegram/SecretInputMedia.h"
#include "td/telegram/StickerPhotoSize.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/MovableValue.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/unique_value_ptr.h"

namespace td {

class FileManager;
class Td;

struct Photo {
  MovableValue<int64, -2> id;
  int32 date = 0;
  string minithumbnail;
  vector<PhotoSize> photos;

  vector<AnimationSize> animations;

  unique_value_ptr<StickerPhotoSize> sticker_photo_size;

  bool has_stickers = false;
  vector<FileId> sticker_file_ids;

  bool is_empty() const {
    return id.get() == -2;
  }

  bool is_bad() const {
    if (is_empty()) {
      return true;
    }
    for (auto &photo_size : photos) {
      if (!photo_size.file_id.is_valid()) {
        return true;
      }
    }
    return false;
  }
};

Photo get_photo(Td *td, tl_object_ptr<telegram_api::Photo> &&photo, DialogId owner_dialog_id,
                FileType file_type = FileType::Photo);

Photo get_photo(Td *td, tl_object_ptr<telegram_api::photo> &&photo, DialogId owner_dialog_id,
                FileType file_type = FileType::Photo);

Photo get_encrypted_file_photo(FileManager *file_manager, unique_ptr<EncryptedFile> &&file,
                               tl_object_ptr<secret_api::decryptedMessageMediaPhoto> &&photo, DialogId owner_dialog_id);

Photo get_web_document_photo(FileManager *file_manager, tl_object_ptr<telegram_api::WebDocument> web_document,
                             DialogId owner_dialog_id);

Result<Photo> create_photo(FileManager *file_manager, FileId file_id, PhotoSize &&thumbnail, int32 width, int32 height,
                           vector<FileId> &&sticker_file_ids);

Photo dup_photo(Photo photo);

tl_object_ptr<td_api::photo> get_photo_object(FileManager *file_manager, const Photo &photo);

void merge_photos(Td *td, const Photo *old_photo, Photo *new_photo, DialogId dialog_id, bool need_merge_files,
                  bool &is_content_changed, bool &need_update);

void photo_delete_thumbnail(Photo &photo);

FileId get_photo_any_file_id(const Photo &photo);

FileId get_photo_thumbnail_file_id(const Photo &photo);

SecretInputMedia photo_get_secret_input_media(FileManager *file_manager, const Photo &photo,
                                              telegram_api::object_ptr<telegram_api::InputEncryptedFile> input_file,
                                              const string &caption, BufferSlice thumbnail);

tl_object_ptr<telegram_api::InputMedia> photo_get_input_media(
    FileManager *file_manager, const Photo &photo, telegram_api::object_ptr<telegram_api::InputFile> input_file,
    int32 ttl, bool has_spoiler);

telegram_api::object_ptr<telegram_api::InputMedia> photo_get_cover_input_media(FileManager *file_manager,
                                                                               const Photo &photo, bool force,
                                                                               bool allow_external);

vector<FileId> photo_get_file_ids(const Photo &photo);

bool operator==(const Photo &lhs, const Photo &rhs);
bool operator!=(const Photo &lhs, const Photo &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const Photo &photo);

}  // namespace td
