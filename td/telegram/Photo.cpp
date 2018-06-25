//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Photo.h"

#include "td/telegram/secret_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/files/FileManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/net/DcId.h"

#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/HttpUrl.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Random.h"

#include <algorithm>
#include <limits>

namespace td {

static uint16 get_dimension(int32 size) {
  if (size < 0 || size > 65535) {
    LOG(ERROR) << "Wrong image dimension = " << size;
    return 0;
  }
  return narrow_cast<uint16>(size);
}

Dimensions get_dimensions(int32 width, int32 height) {
  Dimensions result;
  result.width = get_dimension(width);
  result.height = get_dimension(height);
  if (result.width == 0 || result.height == 0) {
    result.width = 0;
    result.height = 0;
  }
  return result;
}

static uint32 get_pixel_count(const Dimensions &dimensions) {
  return static_cast<uint32>(dimensions.width) * static_cast<uint32>(dimensions.height);
}

bool operator==(const Dimensions &lhs, const Dimensions &rhs) {
  return lhs.width == rhs.width && lhs.height == rhs.height;
}

bool operator!=(const Dimensions &lhs, const Dimensions &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const Dimensions &dimensions) {
  return string_builder << "(" << dimensions.width << ", " << dimensions.height << ")";
}

static FileId register_photo(FileManager *file_manager, FileType file_type, int64 id, int64 access_hash,
                             tl_object_ptr<telegram_api::FileLocation> &&location_ptr, DialogId owner_dialog_id,
                             int32 file_size, bool is_webp = false) {
  int32 location_id = location_ptr->get_id();
  DcId dc_id;
  int32 local_id;
  int64 volume_id;
  int64 secret;
  switch (location_id) {
    case telegram_api::fileLocationUnavailable::ID: {
      auto location = move_tl_object_as<telegram_api::fileLocationUnavailable>(location_ptr);
      dc_id = DcId::invalid();
      local_id = location->local_id_;
      volume_id = location->volume_id_;
      secret = location->secret_;
      break;
    }
    case telegram_api::fileLocation::ID: {
      auto location = move_tl_object_as<telegram_api::fileLocation>(location_ptr);
      if (!DcId::is_valid(location->dc_id_)) {
        dc_id = DcId::invalid();
      } else {
        dc_id = DcId::internal(location->dc_id_);
      }
      local_id = location->local_id_;
      volume_id = location->volume_id_;
      secret = location->secret_;
      break;
    }
    default:
      UNREACHABLE();
      break;
  }

  LOG(DEBUG) << "Receive " << (is_webp ? "webp" : "jpeg") << " photo of type " << static_cast<int8>(file_type)
             << " in [" << dc_id << "," << volume_id << "," << local_id << "]. Id: (" << id << ", " << access_hash
             << ")";
  auto suggested_name = PSTRING() << static_cast<uint64>(volume_id) << "_" << static_cast<uint64>(local_id)
                                  << (is_webp ? ".webp" : ".jpg");
  return file_manager->register_remote(
      FullRemoteFileLocation(file_type, id, access_hash, local_id, volume_id, secret, dc_id),
      FileLocationSource::FromServer, owner_dialog_id, file_size, 0, std::move(suggested_name));
}

ProfilePhoto get_profile_photo(FileManager *file_manager,
                               tl_object_ptr<telegram_api::UserProfilePhoto> &&profile_photo_ptr) {
  ProfilePhoto result;
  int32 profile_photo_id =
      profile_photo_ptr == nullptr ? telegram_api::userProfilePhotoEmpty::ID : profile_photo_ptr->get_id();
  switch (profile_photo_id) {
    case telegram_api::userProfilePhotoEmpty::ID:
      break;
    case telegram_api::userProfilePhoto::ID: {
      auto profile_photo = move_tl_object_as<telegram_api::userProfilePhoto>(profile_photo_ptr);

      result.id = profile_photo->photo_id_;
      result.small_file_id = register_photo(file_manager, FileType::ProfilePhoto, result.id, 0,
                                            std::move(profile_photo->photo_small_), DialogId(), 0);
      result.big_file_id = register_photo(file_manager, FileType::ProfilePhoto, result.id, 0,
                                          std::move(profile_photo->photo_big_), DialogId(), 0);
      break;
    }
    default:
      UNREACHABLE();
      break;
  }

  return result;
}

tl_object_ptr<td_api::profilePhoto> get_profile_photo_object(FileManager *file_manager,
                                                             const ProfilePhoto *profile_photo) {
  if (profile_photo == nullptr || !profile_photo->small_file_id.is_valid()) {
    return nullptr;
  }
  return make_tl_object<td_api::profilePhoto>(profile_photo->id,
                                              file_manager->get_file_object(profile_photo->small_file_id),
                                              file_manager->get_file_object(profile_photo->big_file_id));
}

bool operator==(const ProfilePhoto &lhs, const ProfilePhoto &rhs) {
  bool location_differs = lhs.small_file_id != rhs.small_file_id || lhs.big_file_id != rhs.big_file_id;
  bool id_differs;
  if (lhs.id == -1 && rhs.id == -1) {
    // group chat photo
    id_differs = location_differs;
  } else {
    id_differs = lhs.id != rhs.id;
  }

  if (location_differs) {
    LOG_IF(ERROR, !id_differs) << "location_differs = true, but id_differs = false. First profilePhoto: " << lhs
                               << ", second profilePhoto: " << rhs;
    return false;
  }
  return true;
}

bool operator!=(const ProfilePhoto &lhs, const ProfilePhoto &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const ProfilePhoto &profile_photo) {
  return string_builder << "<id = " << profile_photo.id << ", small_file_id = " << profile_photo.small_file_id
                        << ", big_file_id = " << profile_photo.big_file_id << ">";
}

DialogPhoto get_dialog_photo(FileManager *file_manager, tl_object_ptr<telegram_api::ChatPhoto> &&chat_photo_ptr) {
  int32 chat_photo_id = chat_photo_ptr == nullptr ? telegram_api::chatPhotoEmpty::ID : chat_photo_ptr->get_id();

  DialogPhoto result;
  switch (chat_photo_id) {
    case telegram_api::chatPhotoEmpty::ID:
      break;
    case telegram_api::chatPhoto::ID: {
      auto chat_photo = move_tl_object_as<telegram_api::chatPhoto>(chat_photo_ptr);

      result.small_file_id = register_photo(file_manager, FileType::ProfilePhoto, 0, 0,
                                            std::move(chat_photo->photo_small_), DialogId(), 0);
      result.big_file_id =
          register_photo(file_manager, FileType::ProfilePhoto, 0, 0, std::move(chat_photo->photo_big_), DialogId(), 0);

      break;
    }
    default:
      UNREACHABLE();
      break;
  }

  return result;
}

tl_object_ptr<td_api::chatPhoto> get_chat_photo_object(FileManager *file_manager, const DialogPhoto *dialog_photo) {
  if (dialog_photo == nullptr || !dialog_photo->small_file_id.is_valid()) {
    return nullptr;
  }
  return make_tl_object<td_api::chatPhoto>(file_manager->get_file_object(dialog_photo->small_file_id),
                                           file_manager->get_file_object(dialog_photo->big_file_id));
}

bool operator==(const DialogPhoto &lhs, const DialogPhoto &rhs) {
  return lhs.small_file_id == rhs.small_file_id && lhs.big_file_id == rhs.big_file_id;
}

bool operator!=(const DialogPhoto &lhs, const DialogPhoto &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const DialogPhoto &dialog_photo) {
  return string_builder << "<small_file_id = " << dialog_photo.small_file_id
                        << ", big_file_id = " << dialog_photo.big_file_id << ">";
}

PhotoSize get_thumbnail_photo_size(FileManager *file_manager, BufferSlice bytes, DialogId owner_dialog_id, int32 width,
                                   int32 height) {
  if (bytes.empty()) {
    return PhotoSize();
  }
  PhotoSize res;
  res.type = 't';
  res.dimensions = get_dimensions(width, height);
  res.size = narrow_cast<int32>(bytes.size());

  // generate some random remote location to save
  auto dc_id = DcId::invalid();
  auto local_id = Random::secure_int32();
  auto volume_id = Random::secure_int64();
  auto secret = 0;
  res.file_id = file_manager->register_remote(
      FullRemoteFileLocation(FileType::EncryptedThumbnail, 0, 0, local_id, volume_id, secret, dc_id),
      FileLocationSource::FromServer, owner_dialog_id, res.size, 0,
      PSTRING() << static_cast<uint64>(volume_id) << "_" << static_cast<uint64>(local_id) << ".jpg");
  file_manager->set_content(res.file_id, std::move(bytes));

  return res;
}

PhotoSize get_photo_size(FileManager *file_manager, FileType file_type, int64 id, int64 access_hash,
                         DialogId owner_dialog_id, tl_object_ptr<telegram_api::PhotoSize> &&size_ptr, bool is_webp) {
  tl_object_ptr<telegram_api::FileLocation> location_ptr;
  string type;

  PhotoSize res;
  BufferSlice content;

  int32 size_id = size_ptr->get_id();
  switch (size_id) {
    case telegram_api::photoSizeEmpty::ID:
      return res;
    case telegram_api::photoSize::ID: {
      auto size = move_tl_object_as<telegram_api::photoSize>(size_ptr);

      type = std::move(size->type_);
      location_ptr = std::move(size->location_);
      res.dimensions = get_dimensions(size->w_, size->h_);
      res.size = size->size_;

      break;
    }
    case telegram_api::photoCachedSize::ID: {
      auto size = move_tl_object_as<telegram_api::photoCachedSize>(size_ptr);

      type = std::move(size->type_);
      location_ptr = std::move(size->location_);
      CHECK(size->bytes_.size() <= static_cast<size_t>(std::numeric_limits<int32>::max()));
      res.dimensions = get_dimensions(size->w_, size->h_);
      res.size = static_cast<int32>(size->bytes_.size());

      content = std::move(size->bytes_);

      break;
    }

    default:
      UNREACHABLE();
      break;
  }

  res.file_id = register_photo(file_manager, file_type, id, access_hash, std::move(location_ptr), owner_dialog_id,
                               res.size, is_webp);

  if (!content.empty()) {
    file_manager->set_content(res.file_id, std::move(content));
  }

  if (type.size() != 1) {
    res.type = 0;
    LOG(ERROR) << "Wrong photoSize " << res;
  } else {
    res.type = static_cast<int32>(type[0]);
  }

  return res;
}

PhotoSize get_web_document_photo_size(FileManager *file_manager, FileType file_type, DialogId owner_dialog_id,
                                      tl_object_ptr<telegram_api::WebDocument> web_document_ptr) {
  if (web_document_ptr == nullptr) {
    return {};
  }

  FileId file_id;
  vector<tl_object_ptr<telegram_api::DocumentAttribute>> attributes;
  int32 size = 0;
  switch (web_document_ptr->get_id()) {
    case telegram_api::webDocument::ID: {
      auto web_document = move_tl_object_as<telegram_api::webDocument>(web_document_ptr);
      auto r_http_url = parse_url(web_document->url_);
      if (r_http_url.is_error()) {
        LOG(ERROR) << "Can't parse URL " << web_document->url_;
        return {};
      }
      auto http_url = r_http_url.move_as_ok();
      auto url = http_url.get_url();
      file_id = file_manager->register_remote(FullRemoteFileLocation(file_type, url, web_document->access_hash_),
                                              FileLocationSource::FromServer, owner_dialog_id, 0, web_document->size_,
                                              get_url_query_file_name(http_url.query_));
      size = web_document->size_;
      attributes = std::move(web_document->attributes_);
      break;
    }
    case telegram_api::webDocumentNoProxy::ID: {
      auto web_document = move_tl_object_as<telegram_api::webDocumentNoProxy>(web_document_ptr);
      if (web_document->url_.find('.') == string::npos) {
        LOG(ERROR) << "Receive invalid URL " << web_document->url_;
        return {};
      }

      auto r_file_id = file_manager->from_persistent_id(web_document->url_, file_type);
      if (r_file_id.is_error()) {
        LOG(ERROR) << "Can't register URL: " << r_file_id.error();
        return {};
      }
      file_id = r_file_id.move_as_ok();

      size = web_document->size_;
      attributes = std::move(web_document->attributes_);
      break;
    }
    default:
      UNREACHABLE();
  }
  CHECK(file_id.is_valid());

  Dimensions dimensions;
  for (auto &attribute : attributes) {
    switch (attribute->get_id()) {
      case telegram_api::documentAttributeImageSize::ID: {
        auto image_size = move_tl_object_as<telegram_api::documentAttributeImageSize>(attribute);
        dimensions = get_dimensions(image_size->w_, image_size->h_);
        break;
      }
      case telegram_api::documentAttributeAnimated::ID:
      case telegram_api::documentAttributeHasStickers::ID:
      case telegram_api::documentAttributeSticker::ID:
      case telegram_api::documentAttributeVideo::ID:
      case telegram_api::documentAttributeAudio::ID:
        LOG(ERROR) << "Unexpected web document attribute " << to_string(attribute);
        break;
      case telegram_api::documentAttributeFilename::ID:
        break;
      default:
        UNREACHABLE();
    }
  }

  PhotoSize s;
  s.type = file_type == FileType::Thumbnail ? 't' : 'u';
  s.dimensions = dimensions;
  s.size = size;
  s.file_id = file_id;
  return s;
}

tl_object_ptr<td_api::photoSize> get_photo_size_object(FileManager *file_manager, const PhotoSize *photo_size) {
  if (photo_size == nullptr || !photo_size->file_id.is_valid()) {
    return nullptr;
  }

  return make_tl_object<td_api::photoSize>(
      photo_size->type ? std::string(1, static_cast<char>(photo_size->type))
                       : std::string(),  // TODO replace string type with integer type
      file_manager->get_file_object(photo_size->file_id), photo_size->dimensions.width, photo_size->dimensions.height);
}

void sort_photo_sizes(vector<td_api::object_ptr<td_api::photoSize>> &sizes) {
  std::sort(sizes.begin(), sizes.end(), [](const auto &lhs, const auto &rhs) {
    if (lhs->photo_->expected_size_ != rhs->photo_->expected_size_) {
      return lhs->photo_->expected_size_ < rhs->photo_->expected_size_;
    }
    return static_cast<uint32>(lhs->width_) * static_cast<uint32>(lhs->height_) <
           static_cast<uint32>(rhs->width_) * static_cast<uint32>(rhs->height_);
  });
}

bool operator==(const PhotoSize &lhs, const PhotoSize &rhs) {
  return lhs.type == rhs.type && lhs.dimensions == rhs.dimensions && lhs.size == rhs.size && lhs.file_id == rhs.file_id;
}

bool operator!=(const PhotoSize &lhs, const PhotoSize &rhs) {
  return !(lhs == rhs);
}

bool operator<(const PhotoSize &lhs, const PhotoSize &rhs) {
  if (lhs.size != rhs.size) {
    return lhs.size < rhs.size;
  }
  auto lhs_pixels = get_pixel_count(lhs.dimensions);
  auto rhs_pixels = get_pixel_count(rhs.dimensions);
  if (lhs_pixels != rhs_pixels) {
    return lhs_pixels < rhs_pixels;
  }
  int32 lhs_type = lhs.type == 't' ? -1 : lhs.type;
  int32 rhs_type = rhs.type == 't' ? -1 : rhs.type;
  if (lhs_type != rhs_type) {
    return lhs_type < rhs_type;
  }
  if (lhs.file_id != rhs.file_id) {
    return lhs.file_id.get() < rhs.file_id.get();
  }
  return lhs.dimensions.width < rhs.dimensions.width;
}

StringBuilder &operator<<(StringBuilder &string_builder, const PhotoSize &photo_size) {
  return string_builder << "{type = " << photo_size.type << ", dimensions = " << photo_size.dimensions
                        << ", size = " << photo_size.size << ", file_id = " << photo_size.file_id << "}";
}

Photo get_photo(FileManager *file_manager, tl_object_ptr<telegram_api::encryptedFile> &&file,
                tl_object_ptr<secret_api::decryptedMessageMediaPhoto> &&photo, DialogId owner_dialog_id) {
  CHECK(DcId::is_valid(file->dc_id_));
  FileId file_id = file_manager->register_remote(
      FullRemoteFileLocation(FileType::Encrypted, file->id_, file->access_hash_, DcId::internal(file->dc_id_)),
      FileLocationSource::FromServer, owner_dialog_id, photo->size_, 0,
      PSTRING() << static_cast<uint64>(file->id_) << ".jpg");
  file_manager->set_encryption_key(file_id, FileEncryptionKey{photo->key_.as_slice(), photo->iv_.as_slice()});

  Photo res;
  res.date = 0;

  if (!photo->thumb_.empty()) {
    res.photos.push_back(get_thumbnail_photo_size(file_manager, std::move(photo->thumb_), owner_dialog_id,
                                                  photo->thumb_w_, photo->thumb_h_));
  }

  PhotoSize s;
  s.type = 'i';
  s.dimensions = get_dimensions(photo->w_, photo->h_);
  s.size = photo->size_;
  s.file_id = file_id;
  res.photos.push_back(s);

  return res;
}

Photo get_photo(FileManager *file_manager, tl_object_ptr<telegram_api::photo> &&photo, DialogId owner_dialog_id) {
  Photo res;

  res.id = photo->id_;
  res.date = photo->date_;
  res.has_stickers = (photo->flags_ & telegram_api::photo::HAS_STICKERS_MASK) != 0;

  for (auto &size_ptr : photo->sizes_) {
    res.photos.push_back(get_photo_size(file_manager, FileType::Photo, photo->id_, photo->access_hash_, owner_dialog_id,
                                        std::move(size_ptr), false));
  }

  return res;
}

tl_object_ptr<td_api::photo> get_photo_object(FileManager *file_manager, const Photo *photo) {
  if (photo == nullptr || photo->id == -2) {
    return nullptr;
  }

  vector<td_api::object_ptr<td_api::photoSize>> photos;
  for (auto &photo_size : photo->photos) {
    photos.push_back(get_photo_size_object(file_manager, &photo_size));
  }
  sort_photo_sizes(photos);
  return make_tl_object<td_api::photo>(photo->id, photo->has_stickers, std::move(photos));
}

void photo_delete_thumbnail(Photo &photo) {
  for (size_t i = 0; i < photo.photos.size(); i++) {
    if (photo.photos[i].type == 't') {
      photo.photos.erase(photo.photos.begin() + i);
    }
  }
}

bool photo_has_input_media(FileManager *file_manager, const Photo &photo, bool is_secret) {
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
    return file_view.has_remote_location() || file_view.has_url();
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
    if (file_view.has_remote_location() && !file_view.remote_location().is_web()) {
      int32 flags = 0;
      if (ttl != 0) {
        flags |= telegram_api::inputMediaPhoto::TTL_SECONDS_MASK;
      }
      return make_tl_object<telegram_api::inputMediaPhoto>(flags, file_view.remote_location().as_input_photo(), ttl);
    }
    if (file_view.has_url()) {
      int32 flags = 0;
      if (ttl != 0) {
        flags |= telegram_api::inputMediaPhotoExternal::TTL_SECONDS_MASK;
      }
      LOG(INFO) << "Create inputMediaPhotoExternal with a URL " << file_view.url() << " and ttl " << ttl;
      return make_tl_object<telegram_api::inputMediaPhotoExternal>(flags, file_view.url(), ttl);
    }
    CHECK(!file_view.has_remote_location());
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
    LOG(INFO) << "HAS REMOTE LOCATION";
    input_file = file_view.remote_location().as_input_encrypted_file();
  }
  if (input_file == nullptr) {
    return {};
  }
  if (thumbnail_file_id.is_valid() && thumbnail.empty()) {
    return {};
  }

  return SecretInputMedia{
      std::move(input_file),
      make_tl_object<secret_api::decryptedMessageMediaPhoto>(
          std::move(thumbnail), thumbnail_width, thumbnail_height, width, height, narrow_cast<int32>(file_view.size()),
          BufferSlice(encryption_key.key_slice()), BufferSlice(encryption_key.iv_slice()), caption)};
}

bool operator==(const Photo &lhs, const Photo &rhs) {
  return lhs.id == rhs.id && lhs.photos == rhs.photos;
}

bool operator!=(const Photo &lhs, const Photo &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const Photo &photo) {
  return string_builder << "[id = " << photo.id << ", photos = " << format::as_array(photo.photos) << "]";
}

}  // namespace td
