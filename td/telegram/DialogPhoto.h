//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class FileManager;
struct Photo;

struct DialogPhoto {
  FileId small_file_id;
  FileId big_file_id;
  string minithumbnail;
  bool has_animation = false;
  bool is_personal = false;
};

struct ProfilePhoto final : public DialogPhoto {
  int64 id = 0;
};

int64 get_profile_photo_id(const tl_object_ptr<telegram_api::UserProfilePhoto> &profile_photo_ptr);

ProfilePhoto get_profile_photo(FileManager *file_manager, UserId user_id, int64 user_access_hash,
                               tl_object_ptr<telegram_api::UserProfilePhoto> &&profile_photo_ptr);

tl_object_ptr<td_api::profilePhoto> get_profile_photo_object(FileManager *file_manager,
                                                             const ProfilePhoto &profile_photo);

bool need_update_profile_photo(const ProfilePhoto &from, const ProfilePhoto &to);

StringBuilder &operator<<(StringBuilder &string_builder, const ProfilePhoto &profile_photo);

DialogPhoto get_dialog_photo(FileManager *file_manager, DialogId dialog_id, int64 dialog_access_hash,
                             telegram_api::object_ptr<telegram_api::ChatPhoto> &&chat_photo_ptr);

tl_object_ptr<td_api::chatPhotoInfo> get_chat_photo_info_object(FileManager *file_manager,
                                                                const DialogPhoto *dialog_photo);

DialogPhoto as_fake_dialog_photo(const Photo &photo, DialogId dialog_id, bool is_personal);

DialogPhoto as_dialog_photo(FileManager *file_manager, DialogId dialog_id, int64 dialog_access_hash, const Photo &photo,
                            bool is_personal);

ProfilePhoto as_profile_photo(FileManager *file_manager, UserId user_id, int64 user_access_hash, const Photo &photo,
                              bool is_personal);

bool is_same_dialog_photo(FileManager *file_manager, DialogId dialog_id, const Photo &photo,
                          const DialogPhoto &dialog_photo, bool is_personal);

vector<FileId> dialog_photo_get_file_ids(const DialogPhoto &dialog_photo);

bool need_update_dialog_photo(const DialogPhoto &from, const DialogPhoto &to);

StringBuilder &operator<<(StringBuilder &string_builder, const DialogPhoto &dialog_photo);

tl_object_ptr<td_api::chatPhoto> get_chat_photo_object(FileManager *file_manager, const Photo &photo);

telegram_api::object_ptr<telegram_api::userProfilePhoto> convert_photo_to_profile_photo(
    const telegram_api::object_ptr<telegram_api::photo> &photo, bool is_personal);

}  // namespace td
