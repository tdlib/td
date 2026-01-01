//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
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
#include "td/telegram/Global.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/PhotoFormat.h"
#include "td/telegram/PhotoSizeSource.h"
#include "td/telegram/PhotoSizeType.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/algorithm.h"
#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/overloaded.h"
#include "td/utils/SliceBuilder.h"

namespace td {

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
  s.type = PhotoSizeType('i');
  s.dimensions = get_dimensions(photo->w_, photo->h_, nullptr);
  s.size = photo->size_;
  s.file_id = file_id;
  res.photos.push_back(s);

  return res;
}

Photo get_photo(Td *td, tl_object_ptr<telegram_api::Photo> &&photo, DialogId owner_dialog_id, FileType file_type) {
  if (photo == nullptr || photo->get_id() == telegram_api::photoEmpty::ID) {
    return Photo();
  }
  CHECK(photo->get_id() == telegram_api::photo::ID);
  return get_photo(td, move_tl_object_as<telegram_api::photo>(photo), owner_dialog_id, file_type);
}

Photo get_photo(Td *td, tl_object_ptr<telegram_api::photo> &&photo, DialogId owner_dialog_id, FileType file_type) {
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
    auto photo_size = get_photo_size(td->file_manager_.get(), PhotoSizeSource::thumbnail(file_type, 0), photo->id_,
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
    auto animation =
        process_video_size(td, PhotoSizeSource::thumbnail(file_type, 0), photo->id_, photo->access_hash_,
                           photo->file_reference_.as_slice().str(), dc_id, owner_dialog_id, std::move(size_ptr));
    if (animation.empty()) {
      continue;
    }
    animation.visit(overloaded(
        [&](AnimationSize &&animation_size) {
          if (animation_size.type != 0 && animation_size.dimensions.width == animation_size.dimensions.height) {
            res.animations.push_back(std::move(animation_size));
          }
        },
        [&](unique_ptr<StickerPhotoSize> &&sticker_photo_size) {
          res.sticker_photo_size = std::move(sticker_photo_size);
        }));
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

Result<Photo> create_photo(FileManager *file_manager, FileId file_id, PhotoSize &&thumbnail, int32 width, int32 height,
                           vector<FileId> &&sticker_file_ids) {
  TRY_RESULT(input_photo_size, get_input_photo_size(file_manager, file_id, width, height));

  Photo photo;
  auto file_view = file_manager->get_file_view(file_id);
  const auto *full_remote_location = file_view.get_full_remote_location();
  if (full_remote_location != nullptr && !full_remote_location->is_web()) {
    photo.id = full_remote_location->get_id();
  }
  if (photo.is_empty()) {
    photo.id = 0;
  }
  photo.date = G()->unix_time();
  if (thumbnail.file_id.is_valid()) {
    photo.photos.push_back(std::move(thumbnail));
  }
  photo.photos.push_back(std::move(input_photo_size));
  photo.has_stickers = !sticker_file_ids.empty();
  photo.sticker_file_ids = std::move(sticker_file_ids);
  return std::move(photo);
}

Photo dup_photo(Photo photo) {
  CHECK(!photo.photos.empty());

  // Find 'i' or largest
  PhotoSize input_size;
  for (const auto &size : photo.photos) {
    if (size.type == 'i') {
      input_size = size;
    }
  }
  if (input_size.type == 0) {
    for (const auto &size : photo.photos) {
      if (input_size.type == 0 || input_size < size) {
        input_size = size;
      }
    }
  }

  // Find 't' or smallest
  PhotoSize thumbnail;
  for (const auto &size : photo.photos) {
    if (size.type == 't') {
      thumbnail = size;
    }
  }
  if (thumbnail.type == 0) {
    for (const auto &size : photo.photos) {
      if (size.type != input_size.type && (thumbnail.type == 0 || size < thumbnail)) {
        thumbnail = size;
      }
    }
  }

  Photo result;
  result.id = std::move(photo.id);
  result.date = photo.date;
  result.minithumbnail = std::move(photo.minithumbnail);
  result.has_stickers = photo.has_stickers;
  result.sticker_file_ids = std::move(photo.sticker_file_ids);

  if (thumbnail.type != 0) {
    thumbnail.type = PhotoSizeType('t');
    result.photos.push_back(std::move(thumbnail));
  }
  input_size.type = PhotoSizeType('i');
  result.photos.push_back(std::move(input_size));

  return result;
}

tl_object_ptr<td_api::photo> get_photo_object(FileManager *file_manager, const Photo &photo) {
  if (photo.is_empty()) {
    return nullptr;
  }

  return td_api::make_object<td_api::photo>(photo.has_stickers, get_minithumbnail_object(photo.minithumbnail),
                                            get_photo_sizes_object(file_manager, photo.photos));
}

void merge_photos(Td *td, const Photo *old_photo, Photo *new_photo, DialogId dialog_id, bool need_merge_files,
                  bool &is_content_changed, bool &need_update) {
  if (old_photo->date != new_photo->date) {
    LOG(DEBUG) << "Photo date has changed from " << old_photo->date << " to " << new_photo->date;
    is_content_changed = true;
  }
  if (old_photo->id.get() != new_photo->id.get() || old_photo->minithumbnail != new_photo->minithumbnail) {
    need_update = true;
  }
  if (old_photo->photos != new_photo->photos) {
    LOG(DEBUG) << "Merge photos " << old_photo->photos << " and " << new_photo->photos
               << ", need_merge_files = " << need_merge_files;
    auto new_photos_size = new_photo->photos.size();
    auto old_photos_size = old_photo->photos.size();

    bool need_merge = false;
    if (need_merge_files && (old_photos_size == 1 || (old_photos_size == 2 && old_photo->photos[0].type == 't')) &&
        old_photo->photos.back().type == 'i') {
      // first time get info about sent photo
      if (!new_photo->photos.empty() && new_photo->photos.back().type == 'i') {
        // remove previous 'i' size for the photo if any
        new_photo->photos.pop_back();
      }
      if (!new_photo->photos.empty() && new_photo->photos.back().type == 't') {
        // remove previous 't' size for the photo if any
        new_photo->photos.pop_back();
      }

      // add back 't' and 'i' sizes
      if (old_photos_size == 2) {
        new_photo->photos.push_back(old_photo->photos[0]);
      }
      new_photo->photos.push_back(old_photo->photos.back());
      need_merge = true;
      need_update = true;
    } else {
      // get sent photo again
      if (old_photos_size == 2 + new_photos_size && old_photo->photos[new_photos_size].type == 't') {
        new_photo->photos.push_back(old_photo->photos[new_photos_size]);
      }
      if (old_photos_size == 1 + new_photo->photos.size() && old_photo->photos.back().type == 'i') {
        new_photo->photos.push_back(old_photo->photos.back());
        need_merge = true;
      }
      if (old_photo->photos != new_photo->photos) {
        new_photo->photos.resize(new_photos_size);  // return previous size, because we shouldn't add local photo sizes
        need_merge = false;
        need_update = true;
      }
    }

    LOG(DEBUG) << "Merge photos " << old_photo->photos << " and " << new_photo->photos
               << " with new photos size = " << new_photos_size << ", need_merge = " << need_merge
               << ", need_update = " << need_update;
    if (need_merge && new_photos_size != 0) {
      CHECK(!old_photo->photos.empty());
      CHECK(old_photo->photos.back().type == 'i');
      FileId old_file_id = old_photo->photos.back().file_id;
      FileView old_file_view = td->file_manager_->get_file_view(old_file_id);
      const auto *old_main_remote_location = old_file_view.get_main_remote_location();
      FileId new_file_id = new_photo->photos[0].file_id;
      FileView new_file_view = td->file_manager_->get_file_view(new_file_id);
      const auto *new_full_remote_location = new_file_view.get_full_remote_location();
      CHECK(new_full_remote_location != nullptr);

      LOG(DEBUG) << "Trying to merge old file " << old_file_id << " and new file " << new_file_id;

      if (new_full_remote_location->is_web()) {
        LOG(ERROR) << "Have remote web photo location";
      } else if (old_main_remote_location == nullptr ||
                 old_main_remote_location->get_file_reference() != new_full_remote_location->get_file_reference() ||
                 old_main_remote_location->get_access_hash() != new_full_remote_location->get_access_hash()) {
        FileId file_id = td->file_manager_->register_remote(
            FullRemoteFileLocation(PhotoSizeSource::thumbnail(new_file_view.get_type(), 'i'),
                                   new_full_remote_location->get_id(), new_full_remote_location->get_access_hash(),
                                   DcId::invalid(), new_full_remote_location->get_file_reference().str()),
            FileLocationSource::FromServer, dialog_id, old_photo->photos.back().size, 0, "");
        LOG_STATUS(td->file_manager_->merge(file_id, old_file_id));
      }
    }
  }
}

void photo_delete_thumbnail(Photo &photo) {
  for (size_t i = 0; i < photo.photos.size(); i++) {
    if (photo.photos[i].type == 't') {
      photo.photos.erase(photo.photos.begin() + i);
      return;
    }
  }
}

tl_object_ptr<telegram_api::InputMedia> photo_get_input_media(
    FileManager *file_manager, const Photo &photo, telegram_api::object_ptr<telegram_api::InputFile> input_file,
    int32 ttl, bool has_spoiler) {
  if (!photo.photos.empty()) {
    auto file_id = photo.photos.back().file_id;
    auto file_view = file_manager->get_file_view(file_id);
    if (file_view.is_encrypted()) {
      return nullptr;
    }
    const auto *main_remote_location = file_view.get_main_remote_location();
    if (main_remote_location != nullptr && !main_remote_location->is_web() && input_file == nullptr) {
      int32 flags = 0;
      if (ttl != 0) {
        flags |= telegram_api::inputMediaPhoto::TTL_SECONDS_MASK;
      }
      return make_tl_object<telegram_api::inputMediaPhoto>(flags, has_spoiler, main_remote_location->as_input_photo(),
                                                           ttl);
    }
    const auto *url = file_view.get_url();
    if (url != nullptr) {
      int32 flags = 0;
      if (ttl != 0) {
        flags |= telegram_api::inputMediaPhotoExternal::TTL_SECONDS_MASK;
      }
      LOG(INFO) << "Create inputMediaPhotoExternal with a URL " << *url << " and self-destruct time " << ttl;
      return make_tl_object<telegram_api::inputMediaPhotoExternal>(flags, has_spoiler, *url, ttl);
    }
    if (input_file == nullptr) {
      CHECK(main_remote_location == nullptr);
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

    CHECK(!photo.photos.empty());
    return make_tl_object<telegram_api::inputMediaUploadedPhoto>(flags, has_spoiler, std::move(input_file),
                                                                 std::move(added_stickers), ttl);
  }
  return nullptr;
}

SecretInputMedia photo_get_secret_input_media(FileManager *file_manager, const Photo &photo,
                                              telegram_api::object_ptr<telegram_api::InputEncryptedFile> input_file,
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
  const auto *main_remote_location = file_view.get_main_remote_location();
  if (main_remote_location != nullptr) {
    LOG(INFO) << "Photo has remote location";
    input_file = main_remote_location->as_input_encrypted_file();
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

telegram_api::object_ptr<telegram_api::InputMedia> photo_get_cover_input_media(FileManager *file_manager,
                                                                               const Photo &photo, bool force,
                                                                               bool allow_external) {
  auto input_media = photo_get_input_media(file_manager, photo, nullptr, 0, false);
  if (input_media == nullptr || (!allow_external && input_media->get_id() != telegram_api::inputMediaPhoto::ID)) {
    return nullptr;
  }
  auto file_reference = FileManager::extract_file_reference(input_media);
  if (file_reference == FileReferenceView::invalid_file_reference()) {
    if (!force) {
      LOG(INFO) << "Have invalid file reference for cover " << photo;
      return nullptr;
    }
  }
  return input_media;
}

vector<FileId> photo_get_file_ids(const Photo &photo) {
  auto result = transform(photo.photos, [](auto &size) { return size.file_id; });
  if (!photo.animations.empty()) {
    // photo file IDs must be first
    append(result, transform(photo.animations, [](auto &size) { return size.file_id; }));
  }
  return result;
}

FileId get_photo_any_file_id(const Photo &photo) {
  const auto &sizes = photo.photos;
  if (!sizes.empty()) {
    return sizes.back().file_id;
  }
  return FileId();
}

FileId get_photo_thumbnail_file_id(const Photo &photo) {
  for (auto &size : photo.photos) {
    if (size.type == 't') {
      return size.file_id;
    }
  }
  return FileId();
}

bool operator==(const Photo &lhs, const Photo &rhs) {
  return lhs.id.get() == rhs.id.get() && lhs.photos == rhs.photos && lhs.animations == rhs.animations &&
         lhs.sticker_photo_size == rhs.sticker_photo_size;
}

bool operator!=(const Photo &lhs, const Photo &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const Photo &photo) {
  string_builder << "[ID = " << photo.id.get() << ", date = " << photo.date << ", photos = " << photo.photos;
  if (!photo.animations.empty()) {
    string_builder << ", animations = " << photo.animations;
  }
  if (photo.sticker_photo_size != nullptr) {
    string_builder << ", sticker = " << *photo.sticker_photo_size;
  }
  return string_builder << ']';
}

}  // namespace td
