//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogPhoto.h"

#include "td/telegram/files/FileManager.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/Photo.h"
#include "td/telegram/PhotoFormat.h"
#include "td/telegram/PhotoSize.h"
#include "td/telegram/PhotoSizeSource.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/logging.h"

namespace td {

int64 get_profile_photo_id(const tl_object_ptr<telegram_api::UserProfilePhoto> &profile_photo_ptr) {
  if (profile_photo_ptr != nullptr && profile_photo_ptr->get_id() == telegram_api::userProfilePhoto::ID) {
    return static_cast<const telegram_api::userProfilePhoto *>(profile_photo_ptr.get())->photo_id_;
  }
  return 0;
}

ProfilePhoto get_profile_photo(FileManager *file_manager, UserId user_id, int64 user_access_hash,
                               tl_object_ptr<telegram_api::UserProfilePhoto> &&profile_photo_ptr) {
  ProfilePhoto result;
  int32 profile_photo_id =
      profile_photo_ptr == nullptr ? telegram_api::userProfilePhotoEmpty::ID : profile_photo_ptr->get_id();
  switch (profile_photo_id) {
    case telegram_api::userProfilePhotoEmpty::ID:
      break;
    case telegram_api::userProfilePhoto::ID: {
      auto profile_photo = move_tl_object_as<telegram_api::userProfilePhoto>(profile_photo_ptr);
      if (profile_photo->photo_id_ == 0 || profile_photo->photo_id_ == -2) {
        LOG(ERROR) << "Receive a profile photo without identifier " << to_string(profile_photo);
        break;
      }

      auto dc_id = DcId::create(profile_photo->dc_id_);
      result.has_animation = profile_photo->has_video_;
      result.is_personal = profile_photo->personal_;
      result.id = profile_photo->photo_id_;
      result.minithumbnail = profile_photo->stripped_thumb_.as_slice().str();
      result.small_file_id =
          register_photo_size(file_manager, PhotoSizeSource::dialog_photo(DialogId(user_id), user_access_hash, false),
                              result.id, 0 /*access_hash*/, "" /*file_reference*/, DialogId(), 0 /*file_size*/, dc_id,
                              PhotoFormat::Jpeg, "get_profile_photo small");
      result.big_file_id =
          register_photo_size(file_manager, PhotoSizeSource::dialog_photo(DialogId(user_id), user_access_hash, true),
                              result.id, 0 /*access_hash*/, "" /*file_reference*/, DialogId(), 0 /*file_size*/, dc_id,
                              PhotoFormat::Jpeg, "get_profile_photo big");
      break;
    }
    default:
      UNREACHABLE();
      break;
  }

  return result;
}

tl_object_ptr<td_api::profilePhoto> get_profile_photo_object(FileManager *file_manager,
                                                             const ProfilePhoto &profile_photo) {
  if (!profile_photo.small_file_id.is_valid()) {
    return nullptr;
  }
  return td_api::make_object<td_api::profilePhoto>(
      profile_photo.id, file_manager->get_file_object(profile_photo.small_file_id),
      file_manager->get_file_object(profile_photo.big_file_id), get_minithumbnail_object(profile_photo.minithumbnail),
      profile_photo.has_animation, profile_photo.is_personal);
}

bool need_update_profile_photo(const ProfilePhoto &from, const ProfilePhoto &to) {
  return from.id != to.id || need_update_dialog_photo(from, to);
}

StringBuilder &operator<<(StringBuilder &string_builder, const ProfilePhoto &profile_photo) {
  return string_builder << "<ID = " << profile_photo.id << ", small_file_id = " << profile_photo.small_file_id
                        << ", big_file_id = " << profile_photo.big_file_id
                        << ", has_animation = " << profile_photo.has_animation
                        << ", is_personal = " << profile_photo.is_personal << '>';
}

DialogPhoto get_dialog_photo(FileManager *file_manager, DialogId dialog_id, int64 dialog_access_hash,
                             telegram_api::object_ptr<telegram_api::ChatPhoto> &&chat_photo_ptr) {
  int32 chat_photo_id = chat_photo_ptr == nullptr ? telegram_api::chatPhotoEmpty::ID : chat_photo_ptr->get_id();

  DialogPhoto result;
  switch (chat_photo_id) {
    case telegram_api::chatPhotoEmpty::ID:
      break;
    case telegram_api::chatPhoto::ID: {
      auto chat_photo = telegram_api::move_object_as<telegram_api::chatPhoto>(chat_photo_ptr);

      auto dc_id = DcId::create(chat_photo->dc_id_);
      result.has_animation = chat_photo->has_video_;
      result.is_personal = false;
      result.minithumbnail = chat_photo->stripped_thumb_.as_slice().str();
      result.small_file_id = register_photo_size(
          file_manager, PhotoSizeSource::dialog_photo(dialog_id, dialog_access_hash, false), chat_photo->photo_id_, 0,
          "", DialogId(), 0, dc_id, PhotoFormat::Jpeg, "get_dialog_photo small");
      result.big_file_id = register_photo_size(
          file_manager, PhotoSizeSource::dialog_photo(dialog_id, dialog_access_hash, true), chat_photo->photo_id_, 0,
          "", DialogId(), 0, dc_id, PhotoFormat::Jpeg, "get_dialog_photo big");

      break;
    }
    default:
      UNREACHABLE();
      break;
  }

  return result;
}

tl_object_ptr<td_api::chatPhotoInfo> get_chat_photo_info_object(FileManager *file_manager,
                                                                const DialogPhoto *dialog_photo) {
  if (dialog_photo == nullptr || !dialog_photo->small_file_id.is_valid()) {
    return nullptr;
  }
  return td_api::make_object<td_api::chatPhotoInfo>(file_manager->get_file_object(dialog_photo->small_file_id),
                                                    file_manager->get_file_object(dialog_photo->big_file_id),
                                                    get_minithumbnail_object(dialog_photo->minithumbnail),
                                                    dialog_photo->has_animation, dialog_photo->is_personal);
}

vector<FileId> dialog_photo_get_file_ids(const DialogPhoto &dialog_photo) {
  vector<FileId> result;
  if (dialog_photo.small_file_id.is_valid()) {
    result.push_back(dialog_photo.small_file_id);
  }
  if (dialog_photo.big_file_id.is_valid()) {
    result.push_back(dialog_photo.big_file_id);
  }
  return result;
}

DialogPhoto as_fake_dialog_photo(const Photo &photo, DialogId dialog_id, bool is_personal) {
  DialogPhoto result;
  if (!photo.is_empty()) {
    for (auto &size : photo.photos) {
      if (size.type == 'a') {
        result.small_file_id = size.file_id;
      } else if (size.type == 'c') {
        result.big_file_id = size.file_id;
      }
    }
    result.minithumbnail = photo.minithumbnail;
    result.has_animation = !photo.animations.empty();
    result.is_personal = is_personal;
    if (!result.small_file_id.is_valid() || !result.big_file_id.is_valid()) {
      LOG(ERROR) << "Failed to convert " << photo << " to chat photo of " << dialog_id;
      return DialogPhoto();
    }
  }
  return result;
}

DialogPhoto as_dialog_photo(FileManager *file_manager, DialogId dialog_id, int64 dialog_access_hash, const Photo &photo,
                            bool is_personal) {
  DialogPhoto result;
  static_cast<DialogPhoto &>(result) = as_fake_dialog_photo(photo, dialog_id, is_personal);
  if (!result.small_file_id.is_valid()) {
    return result;
  }

  auto reregister_photo = [&](bool is_big, FileId file_id) {
    auto file_view = file_manager->get_file_view(file_id);
    const auto *full_remote_location = file_view.get_full_remote_location();
    CHECK(full_remote_location != nullptr);
    auto remote = *full_remote_location;
    CHECK(remote.is_photo());
    CHECK(!remote.is_web());
    remote.set_source(PhotoSizeSource::dialog_photo(dialog_id, dialog_access_hash, is_big));
    return file_manager->register_remote(std::move(remote), FileLocationSource::FromServer, DialogId(), 0, 0,
                                         file_view.remote_name());
  };

  result.small_file_id = reregister_photo(false, result.small_file_id);
  result.big_file_id = reregister_photo(true, result.big_file_id);

  return result;
}

ProfilePhoto as_profile_photo(FileManager *file_manager, UserId user_id, int64 user_access_hash, const Photo &photo,
                              bool is_personal) {
  ProfilePhoto result;
  static_cast<DialogPhoto &>(result) =
      as_dialog_photo(file_manager, DialogId(user_id), user_access_hash, photo, is_personal);
  if (result.small_file_id.is_valid()) {
    result.id = photo.id.get();
  }
  return result;
}

bool is_same_dialog_photo(FileManager *file_manager, DialogId dialog_id, const Photo &photo,
                          const DialogPhoto &dialog_photo, bool is_personal) {
  auto get_unique_file_id = [file_manager](FileId file_id) {
    return file_manager->get_file_view(file_id).get_unique_file_id();
  };
  auto fake_photo = as_fake_dialog_photo(photo, dialog_id, is_personal);
  return get_unique_file_id(fake_photo.small_file_id) == get_unique_file_id(dialog_photo.small_file_id) &&
         get_unique_file_id(fake_photo.big_file_id) == get_unique_file_id(dialog_photo.big_file_id);
}

bool need_update_dialog_photo(const DialogPhoto &from, const DialogPhoto &to) {
  return from.small_file_id != to.small_file_id || from.big_file_id != to.big_file_id ||
         from.has_animation != to.has_animation || from.is_personal != to.is_personal;
}

StringBuilder &operator<<(StringBuilder &string_builder, const DialogPhoto &dialog_photo) {
  return string_builder << "<small_file_id = " << dialog_photo.small_file_id
                        << ", big_file_id = " << dialog_photo.big_file_id
                        << ", has_animation = " << dialog_photo.has_animation
                        << ", is_personal = " << dialog_photo.is_personal << '>';
}

static tl_object_ptr<td_api::animatedChatPhoto> get_animated_chat_photo_object(FileManager *file_manager,
                                                                               const AnimationSize *animation_size) {
  if (animation_size == nullptr || !animation_size->file_id.is_valid()) {
    return nullptr;
  }

  return td_api::make_object<td_api::animatedChatPhoto>(animation_size->dimensions.width,
                                                        file_manager->get_file_object(animation_size->file_id),
                                                        animation_size->main_frame_timestamp);
}

tl_object_ptr<td_api::chatPhoto> get_chat_photo_object(FileManager *file_manager, const Photo &photo) {
  if (photo.is_empty()) {
    return nullptr;
  }

  const AnimationSize *small_animation = nullptr;
  const AnimationSize *big_animation = nullptr;
  for (auto &animation : photo.animations) {
    if (animation.type == 'p') {
      small_animation = &animation;
    } else if (animation.type == 'u') {
      big_animation = &animation;
    }
  }
  if (big_animation == nullptr && small_animation != nullptr) {
    LOG(ERROR) << "Have small animation without big animation in " << photo;
    small_animation = nullptr;
  }
  auto chat_photo_sticker =
      photo.sticker_photo_size == nullptr ? nullptr : photo.sticker_photo_size->get_chat_photo_sticker_object();
  return td_api::make_object<td_api::chatPhoto>(
      photo.id.get(), photo.date, get_minithumbnail_object(photo.minithumbnail),
      get_photo_sizes_object(file_manager, photo.photos), get_animated_chat_photo_object(file_manager, big_animation),
      get_animated_chat_photo_object(file_manager, small_animation), std::move(chat_photo_sticker));
}

telegram_api::object_ptr<telegram_api::userProfilePhoto> convert_photo_to_profile_photo(
    const telegram_api::object_ptr<telegram_api::photo> &photo, bool is_personal) {
  if (photo == nullptr) {
    return nullptr;
  }

  bool have_photo_small = false;
  bool have_photo_big = false;
  for (auto &size_ptr : photo->sizes_) {
    switch (size_ptr->get_id()) {
      case telegram_api::photoSizeEmpty::ID:
        break;
      case telegram_api::photoSize::ID: {
        auto size = static_cast<const telegram_api::photoSize *>(size_ptr.get());
        if (size->type_ == "a") {
          have_photo_small = true;
        } else if (size->type_ == "c") {
          have_photo_big = true;
        }
        break;
      }
      case telegram_api::photoCachedSize::ID: {
        auto size = static_cast<const telegram_api::photoCachedSize *>(size_ptr.get());
        if (size->type_ == "a") {
          have_photo_small = true;
        } else if (size->type_ == "c") {
          have_photo_big = true;
        }
        break;
      }
      case telegram_api::photoStrippedSize::ID:
        break;
      case telegram_api::photoSizeProgressive::ID: {
        auto size = static_cast<const telegram_api::photoSizeProgressive *>(size_ptr.get());
        if (size->type_ == "a") {
          have_photo_small = true;
        } else if (size->type_ == "c") {
          have_photo_big = true;
        }
        break;
      }
      default:
        UNREACHABLE();
        break;
    }
  }
  if (!have_photo_small || !have_photo_big) {
    return nullptr;
  }
  bool has_video = !photo->video_sizes_.empty();
  return telegram_api::make_object<telegram_api::userProfilePhoto>(0, has_video, is_personal, photo->id_, BufferSlice(),
                                                                   photo->dc_id_);
}

}  // namespace td
