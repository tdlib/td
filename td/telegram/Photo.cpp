//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Photo.h"

#include "td/telegram/Dimensions.h"
#include "td/telegram/files/FileEncryptionKey.h"
#include "td/telegram/files/FileLocation.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/files/FileType.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/PhotoFormat.h"
#include "td/telegram/PhotoSizeSource.h"

#include "td/utils/algorithm.h"
#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/SliceBuilder.h"

#include <algorithm>

namespace td {

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

      auto dc_id = DcId::create(profile_photo->dc_id_);
      result.has_animation = profile_photo->has_video_;
      result.id = profile_photo->photo_id_;
      result.minithumbnail = profile_photo->stripped_thumb_.as_slice().str();
      result.small_file_id = register_photo_size(
          file_manager, PhotoSizeSource::dialog_photo(DialogId(user_id), user_access_hash, false), result.id,
          0 /*access_hash*/, "" /*file_reference*/, DialogId(), 0 /*file_size*/, dc_id, PhotoFormat::Jpeg);
      result.big_file_id = register_photo_size(
          file_manager, PhotoSizeSource::dialog_photo(DialogId(user_id), user_access_hash, true), result.id,
          0 /*access_hash*/, "" /*file_reference*/, DialogId(), 0 /*file_size*/, dc_id, PhotoFormat::Jpeg);
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
      profile_photo.has_animation);
}

bool need_update_profile_photo(const ProfilePhoto &from, const ProfilePhoto &to) {
  return from.id != to.id || need_update_dialog_photo(from, to);
}

StringBuilder &operator<<(StringBuilder &string_builder, const ProfilePhoto &profile_photo) {
  return string_builder << "<ID = " << profile_photo.id << ", small_file_id = " << profile_photo.small_file_id
                        << ", big_file_id = " << profile_photo.big_file_id
                        << ", has_animation = " << profile_photo.has_animation << ">";
}

DialogPhoto get_dialog_photo(FileManager *file_manager, DialogId dialog_id, int64 dialog_access_hash,
                             tl_object_ptr<telegram_api::ChatPhoto> &&chat_photo_ptr) {
  int32 chat_photo_id = chat_photo_ptr == nullptr ? telegram_api::chatPhotoEmpty::ID : chat_photo_ptr->get_id();

  DialogPhoto result;
  switch (chat_photo_id) {
    case telegram_api::chatPhotoEmpty::ID:
      break;
    case telegram_api::chatPhoto::ID: {
      auto chat_photo = move_tl_object_as<telegram_api::chatPhoto>(chat_photo_ptr);

      auto dc_id = DcId::create(chat_photo->dc_id_);
      result.has_animation = chat_photo->has_video_;
      result.minithumbnail = chat_photo->stripped_thumb_.as_slice().str();
      result.small_file_id =
          register_photo_size(file_manager, PhotoSizeSource::dialog_photo(dialog_id, dialog_access_hash, false),
                              chat_photo->photo_id_, 0, "", DialogId(), 0, dc_id, PhotoFormat::Jpeg);
      result.big_file_id =
          register_photo_size(file_manager, PhotoSizeSource::dialog_photo(dialog_id, dialog_access_hash, true),
                              chat_photo->photo_id_, 0, "", DialogId(), 0, dc_id, PhotoFormat::Jpeg);

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
                                                    dialog_photo->has_animation);
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

DialogPhoto as_fake_dialog_photo(const Photo &photo, DialogId dialog_id) {
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
    if (!result.small_file_id.is_valid() || !result.big_file_id.is_valid()) {
      LOG(ERROR) << "Failed to convert " << photo << " to chat photo of " << dialog_id;
      return DialogPhoto();
    }
  }
  return result;
}

DialogPhoto as_dialog_photo(FileManager *file_manager, DialogId dialog_id, int64 dialog_access_hash,
                            const Photo &photo) {
  DialogPhoto result;
  static_cast<DialogPhoto &>(result) = as_fake_dialog_photo(photo, dialog_id);
  if (!result.small_file_id.is_valid()) {
    return result;
  }

  auto reregister_photo = [&](bool is_big, FileId file_id) {
    auto file_view = file_manager->get_file_view(file_id);
    CHECK(file_view.has_remote_location());
    auto remote = file_view.remote_location();
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

ProfilePhoto as_profile_photo(FileManager *file_manager, UserId user_id, int64 user_access_hash, const Photo &photo) {
  ProfilePhoto result;
  static_cast<DialogPhoto &>(result) = as_dialog_photo(file_manager, DialogId(user_id), user_access_hash, photo);
  if (result.small_file_id.is_valid()) {
    result.id = photo.id.get();
  }
  return result;
}

bool is_same_dialog_photo(FileManager *file_manager, DialogId dialog_id, const Photo &photo,
                          const DialogPhoto &dialog_photo) {
  auto get_unique_file_id = [file_manager](FileId file_id) {
    return file_manager->get_file_view(file_id).get_unique_file_id();
  };
  auto fake_photo = as_fake_dialog_photo(photo, dialog_id);
  return get_unique_file_id(fake_photo.small_file_id) == get_unique_file_id(dialog_photo.small_file_id) &&
         get_unique_file_id(fake_photo.big_file_id) == get_unique_file_id(dialog_photo.big_file_id);
}

bool need_update_dialog_photo(const DialogPhoto &from, const DialogPhoto &to) {
  return from.small_file_id != to.small_file_id || from.big_file_id != to.big_file_id ||
         from.has_animation != to.has_animation ||
         need_update_dialog_photo_minithumbnail(from.minithumbnail, to.minithumbnail);
}

StringBuilder &operator<<(StringBuilder &string_builder, const DialogPhoto &dialog_photo) {
  return string_builder << "<small_file_id = " << dialog_photo.small_file_id
                        << ", big_file_id = " << dialog_photo.big_file_id
                        << ", has_animation = " << dialog_photo.has_animation << ">";
}

static tl_object_ptr<td_api::photoSize> get_photo_size_object(FileManager *file_manager, const PhotoSize *photo_size) {
  if (photo_size == nullptr || !photo_size->file_id.is_valid()) {
    return nullptr;
  }

  return td_api::make_object<td_api::photoSize>(
      photo_size->type ? std::string(1, static_cast<char>(photo_size->type))
                       : std::string(),  // TODO replace string type with integer type
      file_manager->get_file_object(photo_size->file_id), photo_size->dimensions.width, photo_size->dimensions.height,
      vector<int32>(photo_size->progressive_sizes));
}

static vector<td_api::object_ptr<td_api::photoSize>> get_photo_sizes_object(FileManager *file_manager,
                                                                            const vector<PhotoSize> &photo_sizes) {
  auto sizes = transform(photo_sizes, [file_manager](const PhotoSize &photo_size) {
    return get_photo_size_object(file_manager, &photo_size);
  });
  std::stable_sort(sizes.begin(), sizes.end(), [](const auto &lhs, const auto &rhs) {
    if (lhs->photo_->expected_size_ != rhs->photo_->expected_size_) {
      return lhs->photo_->expected_size_ < rhs->photo_->expected_size_;
    }
    return static_cast<uint32>(lhs->width_) * static_cast<uint32>(lhs->height_) <
           static_cast<uint32>(rhs->width_) * static_cast<uint32>(rhs->height_);
  });
  td::remove_if(sizes, [](const auto &size) {
    return !size->photo_->local_->can_be_downloaded_ && !size->photo_->local_->is_downloading_completed_;
  });
  return sizes;
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

Photo get_encrypted_file_photo(FileManager *file_manager, unique_ptr<EncryptedFile> &&file,
                               tl_object_ptr<secret_api::decryptedMessageMediaPhoto> &&photo,
                               DialogId owner_dialog_id) {
  FileId file_id = file_manager->register_remote(
      FullRemoteFileLocation(FileType::Encrypted, file->id_, file->access_hash_, DcId::create(file->dc_id_), string()),
      FileLocationSource::FromServer, owner_dialog_id, photo->size_, 0,
      PSTRING() << static_cast<uint64>(file->id_) << ".jpg");
  file_manager->set_encryption_key(file_id, FileEncryptionKey{photo->key_.as_slice(), photo->iv_.as_slice()});

  Photo res;
  res.id = 0;
  res.date = 0;

  if (!photo->thumb_.empty()) {
    res.photos.push_back(get_secret_thumbnail_photo_size(file_manager, std::move(photo->thumb_), owner_dialog_id,
                                                         photo->thumb_w_, photo->thumb_h_));
  }

  PhotoSize s;
  s.type = 'i';
  s.dimensions = get_dimensions(photo->w_, photo->h_, nullptr);
  s.size = photo->size_;
  s.file_id = file_id;
  res.photos.push_back(s);

  return res;
}

Photo get_photo(FileManager *file_manager, tl_object_ptr<telegram_api::Photo> &&photo, DialogId owner_dialog_id) {
  if (photo == nullptr || photo->get_id() == telegram_api::photoEmpty::ID) {
    return Photo();
  }
  CHECK(photo->get_id() == telegram_api::photo::ID);
  return get_photo(file_manager, move_tl_object_as<telegram_api::photo>(photo), owner_dialog_id);
}

Photo get_photo(FileManager *file_manager, tl_object_ptr<telegram_api::photo> &&photo, DialogId owner_dialog_id) {
  CHECK(photo != nullptr);
  Photo res;

  res.id = photo->id_;
  res.date = photo->date_;
  res.has_stickers = photo->has_stickers_;

  if (res.is_empty()) {
    LOG(ERROR) << "Receive photo with identifier " << res.id.get();
    res.id = -3;
  }

  DcId dc_id = DcId::create(photo->dc_id_);
  for (auto &size_ptr : photo->sizes_) {
    auto photo_size = get_photo_size(file_manager, PhotoSizeSource::thumbnail(FileType::Photo, 0), photo->id_,
                                     photo->access_hash_, photo->file_reference_.as_slice().str(), dc_id,
                                     owner_dialog_id, std::move(size_ptr), PhotoFormat::Jpeg);
    if (photo_size.get_offset() == 0) {
      PhotoSize &size = photo_size.get<0>();
      if (size.type == 0 || size.type == 't' || size.type == 'i' || size.type == 'p' || size.type == 'u' ||
          size.type == 'v') {
        LOG(ERROR) << "Skip unallowed photo size " << size;
        continue;
      }
      res.photos.push_back(std::move(size));
    } else {
      res.minithumbnail = std::move(photo_size.get<1>());
    }
  }

  for (auto &size_ptr : photo->video_sizes_) {
    auto animation = get_animation_size(file_manager, PhotoSizeSource::thumbnail(FileType::Photo, 0), photo->id_,
                                        photo->access_hash_, photo->file_reference_.as_slice().str(), dc_id,
                                        owner_dialog_id, std::move(size_ptr));
    if (animation.type != 0 && animation.dimensions.width == animation.dimensions.height) {
      res.animations.push_back(std::move(animation));
    }
  }

  return res;
}

Photo get_web_document_photo(FileManager *file_manager, tl_object_ptr<telegram_api::WebDocument> web_document,
                             DialogId owner_dialog_id) {
  PhotoSize s = get_web_document_photo_size(file_manager, FileType::Photo, owner_dialog_id, std::move(web_document));
  Photo photo;
  if (s.file_id.is_valid() && s.type != 'v' && s.type != 'g') {
    photo.id = 0;
    photo.photos.push_back(s);
  }
  return photo;
}

tl_object_ptr<td_api::photo> get_photo_object(FileManager *file_manager, const Photo &photo) {
  if (photo.is_empty()) {
    return nullptr;
  }

  return td_api::make_object<td_api::photo>(photo.has_stickers, get_minithumbnail_object(photo.minithumbnail),
                                            get_photo_sizes_object(file_manager, photo.photos));
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
  return td_api::make_object<td_api::chatPhoto>(
      photo.id.get(), photo.date, get_minithumbnail_object(photo.minithumbnail),
      get_photo_sizes_object(file_manager, photo.photos), get_animated_chat_photo_object(file_manager, big_animation),
      get_animated_chat_photo_object(file_manager, small_animation));
}

void photo_delete_thumbnail(Photo &photo) {
  for (size_t i = 0; i < photo.photos.size(); i++) {
    if (photo.photos[i].type == 't') {
      photo.photos.erase(photo.photos.begin() + i);
      return;
    }
  }
}

bool photo_has_input_media(FileManager *file_manager, const Photo &photo, bool is_secret, bool is_bot) {
  if (photo.photos.empty() || photo.photos.back().type != 'i') {
    LOG(ERROR) << "Wrong photo: " << photo;
    return false;
  }
  auto file_id = photo.photos.back().file_id;
  auto file_view = file_manager->get_file_view(file_id);
  if (is_secret) {
    if (!file_view.is_encrypted_secret() || !file_view.has_remote_location()) {
      return false;
    }

    for (const auto &size : photo.photos) {
      if (size.type == 't' && size.file_id.is_valid()) {
        return false;
      }
    }

    return true;
  } else {
    if (file_view.is_encrypted()) {
      return false;
    }
    if (is_bot && file_view.has_remote_location()) {
      return true;
    }
    return /* file_view.has_remote_location() || */ file_view.has_url();
  }
}

tl_object_ptr<telegram_api::InputMedia> photo_get_input_media(FileManager *file_manager, const Photo &photo,
                                                              tl_object_ptr<telegram_api::InputFile> input_file,
                                                              int32 ttl) {
  if (!photo.photos.empty()) {
    auto file_id = photo.photos.back().file_id;
    auto file_view = file_manager->get_file_view(file_id);
    if (file_view.is_encrypted()) {
      return nullptr;
    }
    if (file_view.has_remote_location() && !file_view.main_remote_location().is_web() && input_file == nullptr) {
      int32 flags = 0;
      if (ttl != 0) {
        flags |= telegram_api::inputMediaPhoto::TTL_SECONDS_MASK;
      }
      return make_tl_object<telegram_api::inputMediaPhoto>(flags, file_view.main_remote_location().as_input_photo(),
                                                           ttl);
    }
    if (file_view.has_url()) {
      int32 flags = 0;
      if (ttl != 0) {
        flags |= telegram_api::inputMediaPhotoExternal::TTL_SECONDS_MASK;
      }
      LOG(INFO) << "Create inputMediaPhotoExternal with a URL " << file_view.url() << " and TTL " << ttl;
      return make_tl_object<telegram_api::inputMediaPhotoExternal>(flags, file_view.url(), ttl);
    }
    if (input_file == nullptr) {
      CHECK(!file_view.has_remote_location());
    }
  }
  if (input_file != nullptr) {
    int32 flags = 0;
    vector<tl_object_ptr<telegram_api::InputDocument>> added_stickers;
    if (photo.has_stickers) {
      flags |= telegram_api::inputMediaUploadedPhoto::STICKERS_MASK;
      added_stickers = file_manager->get_input_documents(photo.sticker_file_ids);
    }
    if (ttl != 0) {
      flags |= telegram_api::inputMediaUploadedPhoto::TTL_SECONDS_MASK;
    }

    return make_tl_object<telegram_api::inputMediaUploadedPhoto>(flags, std::move(input_file),
                                                                 std::move(added_stickers), ttl);
  }
  return nullptr;
}

SecretInputMedia photo_get_secret_input_media(FileManager *file_manager, const Photo &photo,
                                              tl_object_ptr<telegram_api::InputEncryptedFile> input_file,
                                              const string &caption, BufferSlice thumbnail) {
  FileId file_id;
  int32 width = 0;
  int32 height = 0;

  FileId thumbnail_file_id;
  int32 thumbnail_width = 0;
  int32 thumbnail_height = 0;
  for (const auto &size : photo.photos) {
    if (size.type == 'i') {
      file_id = size.file_id;
      width = size.dimensions.width;
      height = size.dimensions.height;
    }
    if (size.type == 't') {
      thumbnail_file_id = size.file_id;
      thumbnail_width = size.dimensions.width;
      thumbnail_height = size.dimensions.height;
    }
  }
  if (file_id.empty()) {
    LOG(ERROR) << "NO SIZE";
    return {};
  }
  auto file_view = file_manager->get_file_view(file_id);
  auto &encryption_key = file_view.encryption_key();
  if (!file_view.is_encrypted_secret() || encryption_key.empty()) {
    return {};
  }
  if (file_view.has_remote_location()) {
    LOG(INFO) << "Photo has remote location";
    input_file = file_view.main_remote_location().as_input_encrypted_file();
  }
  if (input_file == nullptr) {
    return {};
  }
  if (thumbnail_file_id.is_valid() && thumbnail.empty()) {
    return {};
  }
  auto size = file_view.size();
  if (size < 0 || size >= 1000000000) {
    size = 0;
  }

  return SecretInputMedia{
      std::move(input_file),
      make_tl_object<secret_api::decryptedMessageMediaPhoto>(
          std::move(thumbnail), thumbnail_width, thumbnail_height, width, height, static_cast<int32>(size),
          BufferSlice(encryption_key.key_slice()), BufferSlice(encryption_key.iv_slice()), caption)};
}

vector<FileId> photo_get_file_ids(const Photo &photo) {
  auto result = transform(photo.photos, [](auto &size) { return size.file_id; });
  if (!photo.animations.empty()) {
    // photo file IDs must be first
    append(result, transform(photo.animations, [](auto &size) { return size.file_id; }));
  }
  return result;
}

bool operator==(const Photo &lhs, const Photo &rhs) {
  return lhs.id.get() == rhs.id.get() && lhs.photos == rhs.photos && lhs.animations == rhs.animations;
}

bool operator!=(const Photo &lhs, const Photo &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const Photo &photo) {
  string_builder << "[ID = " << photo.id.get() << ", photos = " << format::as_array(photo.photos);
  if (!photo.animations.empty()) {
    string_builder << ", animations = " << format::as_array(photo.animations);
  }
  return string_builder << ']';
}

tl_object_ptr<telegram_api::userProfilePhoto> convert_photo_to_profile_photo(
    const tl_object_ptr<telegram_api::photo> &photo) {
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
  return make_tl_object<telegram_api::userProfilePhoto>(0, has_video, photo->id_, BufferSlice(), photo->dc_id_);
}

}  // namespace td
