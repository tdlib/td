//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Photo.h"

#include "td/telegram/secret_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/files/FileEncryptionKey.h"
#include "td/telegram/files/FileLocation.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/net/DcId.h"

#include "td/utils/base64.h"
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

td_api::object_ptr<td_api::minithumbnail> get_minithumbnail_object(const string &packed) {
  if (packed.size() < 3) {
    return nullptr;
  }
  if (packed[0] == '\x01') {
    static const string header =
        base64_decode(
            "/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDACgcHiMeGSgjISMtKygwPGRBPDc3PHtYXUlkkYCZlo+AjIqgtObDoKrarYqMyP/L2u71////"
            "m8H///"
            "/6/+b9//j/2wBDASstLTw1PHZBQXb4pYyl+Pj4+Pj4+Pj4+Pj4+Pj4+Pj4+Pj4+Pj4+Pj4+Pj4+Pj4+Pj4+Pj4+Pj4+Pj4+Pj4+Pj/"
            "wAARCAAAAAADASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/"
            "8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0R"
            "FRkd"
            "ISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2"
            "uHi4"
            "+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/"
            "8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkN"
            "ERUZ"
            "HSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2"
            "Nna4"
            "uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwA=")
            .move_as_ok();
    static const string footer = base64_decode("/9k=").move_as_ok();
    auto result = td_api::make_object<td_api::minithumbnail>();
    result->height_ = static_cast<unsigned char>(packed[1]);
    result->width_ = static_cast<unsigned char>(packed[2]);
    result->data_ = PSTRING() << header.substr(0, 164) << packed[1] << header[165] << packed[2] << header.substr(167)
                              << packed.substr(3) << footer;
    return result;
  }
  return nullptr;
}

static FileId register_photo(FileManager *file_manager, const PhotoSizeSource &source, int64 id, int64 access_hash,
                             std::string file_reference,
                             tl_object_ptr<telegram_api::fileLocationToBeDeprecated> &&location,
                             DialogId owner_dialog_id, int32 file_size, DcId dc_id, bool is_webp = false,
                             bool is_png = false) {
  int32 local_id = location->local_id_;
  int64 volume_id = location->volume_id_;
  LOG(DEBUG) << "Receive " << (is_webp ? "webp" : (is_png ? "png" : "jpeg")) << " photo of type "
             << source.get_file_type() << " in [" << dc_id << "," << volume_id << "," << local_id << "]. Id: (" << id
             << ", " << access_hash << ")";
  auto suggested_name = PSTRING() << static_cast<uint64>(volume_id) << "_" << static_cast<uint64>(local_id)
                                  << (is_webp ? ".webp" : (is_png ? ".png" : ".jpg"));
  auto file_location_source = owner_dialog_id.get_type() == DialogType::SecretChat ? FileLocationSource::FromUser
                                                                                   : FileLocationSource::FromServer;
  return file_manager->register_remote(
      FullRemoteFileLocation(source, id, access_hash, local_id, volume_id, dc_id, std::move(file_reference)),
      file_location_source, owner_dialog_id, file_size, 0, std::move(suggested_name));
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

      auto dc_id = DcId::create(profile_photo->dc_id_);
      result.id = profile_photo->photo_id_;
      result.small_file_id = register_photo(file_manager, {DialogId(user_id), user_access_hash, false}, result.id, 0,
                                            "", std::move(profile_photo->photo_small_), DialogId(), 0, dc_id);
      result.big_file_id = register_photo(file_manager, {DialogId(user_id), user_access_hash, true}, result.id, 0, "",
                                          std::move(profile_photo->photo_big_), DialogId(), 0, dc_id);
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
  return td_api::make_object<td_api::profilePhoto>(profile_photo->id,
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
    LOG_IF(ERROR, !id_differs) << "Photo " << lhs.id << " location has changed. First profilePhoto: " << lhs
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
      result.small_file_id = register_photo(file_manager, {dialog_id, dialog_access_hash, false}, 0, 0, "",
                                            std::move(chat_photo->photo_small_), DialogId(), 0, dc_id);
      result.big_file_id = register_photo(file_manager, {dialog_id, dialog_access_hash, true}, 0, 0, "",
                                          std::move(chat_photo->photo_big_), DialogId(), 0, dc_id);

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
  return td_api::make_object<td_api::chatPhoto>(file_manager->get_file_object(dialog_photo->small_file_id),
                                                file_manager->get_file_object(dialog_photo->big_file_id));
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

DialogPhoto as_dialog_photo(const Photo &photo) {
  DialogPhoto result;
  if (photo.id != -2) {
    for (auto &size : photo.photos) {
      if (size.type == 'a') {
        result.small_file_id = size.file_id;
      } else if (size.type == 'c') {
        result.big_file_id = size.file_id;
      }
    }
    if (!result.small_file_id.is_valid() || !result.big_file_id.is_valid()) {
      LOG(ERROR) << "Failed to convert " << photo << " to chat photo";
      return DialogPhoto();
    }
  }
  return result;
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

PhotoSize get_secret_thumbnail_photo_size(FileManager *file_manager, BufferSlice bytes, DialogId owner_dialog_id,
                                          int32 width, int32 height) {
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

  res.file_id = file_manager->register_remote(
      FullRemoteFileLocation(PhotoSizeSource(FileType::EncryptedThumbnail, 't'), 0, 0, local_id, volume_id, dc_id,
                             string()),
      FileLocationSource::FromServer, owner_dialog_id, res.size, 0,
      PSTRING() << static_cast<uint64>(volume_id) << "_" << static_cast<uint64>(local_id) << ".jpg");
  file_manager->set_content(res.file_id, std::move(bytes));

  return res;
}

Variant<PhotoSize, string> get_photo_size(FileManager *file_manager, PhotoSizeSource source, int64 id,
                                          int64 access_hash, std::string file_reference, DcId dc_id,
                                          DialogId owner_dialog_id, tl_object_ptr<telegram_api::PhotoSize> &&size_ptr,
                                          bool is_webp, bool is_png) {
  CHECK(size_ptr != nullptr);

  tl_object_ptr<telegram_api::fileLocationToBeDeprecated> location;
  string type;
  PhotoSize res;
  BufferSlice content;
  switch (size_ptr->get_id()) {
    case telegram_api::photoSizeEmpty::ID:
      return std::move(res);
    case telegram_api::photoSize::ID: {
      auto size = move_tl_object_as<telegram_api::photoSize>(size_ptr);

      type = std::move(size->type_);
      location = std::move(size->location_);
      res.dimensions = get_dimensions(size->w_, size->h_);
      res.size = size->size_;

      break;
    }
    case telegram_api::photoCachedSize::ID: {
      auto size = move_tl_object_as<telegram_api::photoCachedSize>(size_ptr);

      type = std::move(size->type_);
      location = std::move(size->location_);
      CHECK(size->bytes_.size() <= static_cast<size_t>(std::numeric_limits<int32>::max()));
      res.dimensions = get_dimensions(size->w_, size->h_);
      res.size = static_cast<int32>(size->bytes_.size());

      content = std::move(size->bytes_);

      break;
    }
    case telegram_api::photoStrippedSize::ID: {
      auto size = move_tl_object_as<telegram_api::photoStrippedSize>(size_ptr);
      return size->bytes_.as_slice().str();
    }
    default:
      UNREACHABLE();
      break;
  }

  if (type.size() != 1) {
    res.type = 0;
    LOG(ERROR) << "Wrong photoSize \"" << type << "\" " << res;
  } else {
    res.type = static_cast<uint8>(type[0]);
  }
  if (source.get_type() == PhotoSizeSource::Type::Thumbnail) {
    source.thumbnail().thumbnail_type = res.type;
  }

  res.file_id = register_photo(file_manager, source, id, access_hash, file_reference, std::move(location),
                               owner_dialog_id, res.size, dc_id, is_webp, is_png);

  if (!content.empty()) {
    file_manager->set_content(res.file_id, std::move(content));
  }

  return std::move(res);
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

  return td_api::make_object<td_api::photoSize>(
      photo_size->type ? std::string(1, static_cast<char>(photo_size->type))
                       : std::string(),  // TODO replace string type with integer type
      file_manager->get_file_object(photo_size->file_id), photo_size->dimensions.width, photo_size->dimensions.height);
}

vector<td_api::object_ptr<td_api::photoSize>> get_photo_sizes_object(FileManager *file_manager,
                                                                     const vector<PhotoSize> &photo_sizes) {
  auto sizes = transform(photo_sizes, [file_manager](const PhotoSize &photo_size) {
    return get_photo_size_object(file_manager, &photo_size);
  });
  std::sort(sizes.begin(), sizes.end(), [](const auto &lhs, const auto &rhs) {
    if (lhs->photo_->expected_size_ != rhs->photo_->expected_size_) {
      return lhs->photo_->expected_size_ < rhs->photo_->expected_size_;
    }
    return static_cast<uint32>(lhs->width_) * static_cast<uint32>(lhs->height_) <
           static_cast<uint32>(rhs->width_) * static_cast<uint32>(rhs->height_);
  });
  return sizes;
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

Photo get_encrypted_file_photo(FileManager *file_manager, tl_object_ptr<telegram_api::encryptedFile> &&file,
                               tl_object_ptr<secret_api::decryptedMessageMediaPhoto> &&photo,
                               DialogId owner_dialog_id) {
  FileId file_id = file_manager->register_remote(
      FullRemoteFileLocation(FileType::Encrypted, file->id_, file->access_hash_, DcId::create(file->dc_id_), string()),
      FileLocationSource::FromServer, owner_dialog_id, photo->size_, 0,
      PSTRING() << static_cast<uint64>(file->id_) << ".jpg");
  file_manager->set_encryption_key(file_id, FileEncryptionKey{photo->key_.as_slice(), photo->iv_.as_slice()});

  Photo res;
  res.date = 0;

  if (!photo->thumb_.empty()) {
    res.photos.push_back(get_secret_thumbnail_photo_size(file_manager, std::move(photo->thumb_), owner_dialog_id,
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

Photo get_photo(FileManager *file_manager, tl_object_ptr<telegram_api::Photo> &&photo, DialogId owner_dialog_id) {
  if (photo == nullptr || photo->get_id() == telegram_api::photoEmpty::ID) {
    Photo result;
    result.id = -2;
    return result;
  }
  CHECK(photo->get_id() == telegram_api::photo::ID);
  return get_photo(file_manager, move_tl_object_as<telegram_api::photo>(photo), owner_dialog_id);
}

Photo get_photo(FileManager *file_manager, tl_object_ptr<telegram_api::photo> &&photo, DialogId owner_dialog_id) {
  Photo res;

  res.id = photo->id_;
  res.date = photo->date_;
  res.has_stickers = (photo->flags_ & telegram_api::photo::HAS_STICKERS_MASK) != 0;

  if (res.id == -2) {
    LOG(ERROR) << "Receive photo with id " << res.id;
    res.id = -3;
  }

  for (auto &size_ptr : photo->sizes_) {
    auto photo_size = get_photo_size(file_manager, {FileType::Photo, 0}, photo->id_, photo->access_hash_,
                                     photo->file_reference_.as_slice().str(), DcId::create(photo->dc_id_),
                                     owner_dialog_id, std::move(size_ptr), false, false);
    if (photo_size.get_offset() == 0) {
      PhotoSize &size = photo_size.get<0>();
      if (size.type == 0 || size.type == 't' || size.type == 'i') {
        LOG(ERROR) << "Skip unallowed photo size " << size;
        continue;
      }
      res.photos.push_back(std::move(size));
    } else {
      res.minithumbnail = std::move(photo_size.get<1>());
    }
  }

  return res;
}

Photo get_web_document_photo(FileManager *file_manager, tl_object_ptr<telegram_api::WebDocument> web_document,
                             DialogId owner_dialog_id) {
  PhotoSize s = get_web_document_photo_size(file_manager, FileType::Photo, owner_dialog_id, std::move(web_document));
  Photo photo;
  if (!s.file_id.is_valid()) {
    photo.id = -2;
  } else {
    photo.id = 0;
    photo.photos.push_back(s);
  }
  return photo;
}

tl_object_ptr<td_api::photo> get_photo_object(FileManager *file_manager, const Photo *photo) {
  if (photo == nullptr || photo->id == -2) {
    return nullptr;
  }

  return td_api::make_object<td_api::photo>(photo->has_stickers, get_minithumbnail_object(photo->minithumbnail),
                                            get_photo_sizes_object(file_manager, photo->photos));
}

tl_object_ptr<td_api::userProfilePhoto> get_user_profile_photo_object(FileManager *file_manager, const Photo *photo) {
  if (photo == nullptr || photo->id == -2) {
    return nullptr;
  }

  return td_api::make_object<td_api::userProfilePhoto>(photo->id, photo->date,
                                                       get_photo_sizes_object(file_manager, photo->photos));
}

void photo_delete_thumbnail(Photo &photo) {
  for (size_t i = 0; i < photo.photos.size(); i++) {
    if (photo.photos[i].type == 't') {
      photo.photos.erase(photo.photos.begin() + i);
      return;
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
      LOG(INFO) << "Create inputMediaPhotoExternal with a URL " << file_view.url() << " and ttl " << ttl;
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

  return SecretInputMedia{
      std::move(input_file),
      make_tl_object<secret_api::decryptedMessageMediaPhoto>(
          std::move(thumbnail), thumbnail_width, thumbnail_height, width, height, narrow_cast<int32>(file_view.size()),
          BufferSlice(encryption_key.key_slice()), BufferSlice(encryption_key.iv_slice()), caption)};
}

vector<FileId> photo_get_file_ids(const Photo &photo) {
  return transform(photo.photos, [](auto &size) { return size.file_id; });
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

static tl_object_ptr<telegram_api::fileLocationToBeDeprecated> copy_location(
    const tl_object_ptr<telegram_api::fileLocationToBeDeprecated> &location) {
  CHECK(location != nullptr);
  return make_tl_object<telegram_api::fileLocationToBeDeprecated>(location->volume_id_, location->local_id_);
}

tl_object_ptr<telegram_api::userProfilePhoto> convert_photo_to_profile_photo(
    const tl_object_ptr<telegram_api::photo> &photo) {
  if (photo == nullptr) {
    return nullptr;
  }

  tl_object_ptr<telegram_api::fileLocationToBeDeprecated> photo_small;
  tl_object_ptr<telegram_api::fileLocationToBeDeprecated> photo_big;
  for (auto &size_ptr : photo->sizes_) {
    switch (size_ptr->get_id()) {
      case telegram_api::photoSizeEmpty::ID:
        break;
      case telegram_api::photoSize::ID: {
        auto size = static_cast<const telegram_api::photoSize *>(size_ptr.get());
        if (size->type_ == "a") {
          photo_small = copy_location(size->location_);
        } else if (size->type_ == "c") {
          photo_big = copy_location(size->location_);
        }
        break;
      }
      case telegram_api::photoCachedSize::ID: {
        auto size = static_cast<const telegram_api::photoCachedSize *>(size_ptr.get());
        if (size->type_ == "a") {
          photo_small = copy_location(size->location_);
        } else if (size->type_ == "c") {
          photo_big = copy_location(size->location_);
        }
        break;
      }
      case telegram_api::photoStrippedSize::ID:
        break;
      default:
        UNREACHABLE();
        break;
    }
  }
  if (photo_small == nullptr || photo_big == nullptr) {
    return nullptr;
  }
  return make_tl_object<telegram_api::userProfilePhoto>(photo->id_, std::move(photo_small), std::move(photo_big),
                                                        photo->dc_id_);
}

}  // namespace td
