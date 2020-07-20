//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/telegram_api.h"

#include "td/telegram/files/FileBitmask.h"
#include "td/telegram/files/FileType.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/PhotoSizeSource.h"

#include "td/utils/base64.h"
#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/Slice.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/Variant.h"

#include <tuple>
#include <utility>

namespace td {

class FileReferenceView {
 public:
  static Slice invalid_file_reference() {
    return Slice("#");
  }
};

struct EmptyRemoteFileLocation {
  template <class StorerT>
  void store(StorerT &storer) const {
  }
  template <class ParserT>
  void parse(ParserT &parser) {
  }
};

inline bool operator==(const EmptyRemoteFileLocation &lhs, const EmptyRemoteFileLocation &rhs) {
  return true;
}

inline bool operator!=(const EmptyRemoteFileLocation &lhs, const EmptyRemoteFileLocation &rhs) {
  return !(lhs == rhs);
}

struct PartialRemoteFileLocation {
  int64 file_id_;
  int32 part_count_;
  int32 part_size_;
  int32 ready_part_count_;
  int32 is_big_;

  template <class StorerT>
  void store(StorerT &storer) const;
  template <class ParserT>
  void parse(ParserT &parser);
};

inline bool operator==(const PartialRemoteFileLocation &lhs, const PartialRemoteFileLocation &rhs) {
  return lhs.file_id_ == rhs.file_id_ && lhs.part_count_ == rhs.part_count_ && lhs.part_size_ == rhs.part_size_ &&
         lhs.ready_part_count_ == rhs.ready_part_count_ && lhs.is_big_ == rhs.is_big_;
}

inline bool operator!=(const PartialRemoteFileLocation &lhs, const PartialRemoteFileLocation &rhs) {
  return !(lhs == rhs);
}

inline StringBuilder &operator<<(StringBuilder &sb, const PartialRemoteFileLocation &location) {
  return sb << '[' << (location.is_big_ ? "Big" : "Small") << " partial remote location with " << location.part_count_
            << " parts of size " << location.part_size_ << " with " << location.ready_part_count_ << " ready parts]";
}

struct PhotoRemoteFileLocation {
  int64 id_;
  int64 access_hash_;
  int64 volume_id_;
  int32 local_id_;
  PhotoSizeSource source_;

  template <class StorerT>
  void store(StorerT &storer) const;
  template <class ParserT>
  void parse(ParserT &parser);

  struct AsKey {
    const PhotoRemoteFileLocation &key;
    bool is_unique;

    template <class StorerT>
    void store(StorerT &storer) const;
  };
  AsKey as_key(bool is_unique) const {
    return AsKey{*this, is_unique};
  }

  bool operator<(const PhotoRemoteFileLocation &other) const {
    return std::tie(id_, volume_id_, local_id_) < std::tie(other.id_, other.volume_id_, other.local_id_);
  }
  bool operator==(const PhotoRemoteFileLocation &other) const {
    return std::tie(id_, volume_id_, local_id_) == std::tie(other.id_, other.volume_id_, other.local_id_);
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, const PhotoRemoteFileLocation &location) {
  return string_builder << "[id = " << location.id_ << ", access_hash = " << location.access_hash_
                        << ", volume_id = " << location.volume_id_ << ", local_id = " << location.local_id_ << "]";
}

struct WebRemoteFileLocation {
  string url_;
  int64 access_hash_;

  template <class StorerT>
  void store(StorerT &storer) const;
  template <class ParserT>
  void parse(ParserT &parser);

  struct AsKey {
    const WebRemoteFileLocation &key;

    template <class StorerT>
    void store(StorerT &storer) const;
  };
  AsKey as_key(bool /*is_unique*/) const {
    return AsKey{*this};
  }

  bool operator<(const WebRemoteFileLocation &other) const {
    return url_ < other.url_;
  }
  bool operator==(const WebRemoteFileLocation &other) const {
    return url_ == other.url_;
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, const WebRemoteFileLocation &location) {
  return string_builder << "[url = " << location.url_ << ", access_hash = " << location.access_hash_ << "]";
}

struct CommonRemoteFileLocation {
  int64 id_;
  int64 access_hash_;

  template <class StorerT>
  void store(StorerT &storer) const;
  template <class ParserT>
  void parse(ParserT &parser);

  struct AsKey {
    const CommonRemoteFileLocation &key;

    template <class StorerT>
    void store(StorerT &storer) const;
  };
  AsKey as_key(bool /*is_unique*/) const {
    return AsKey{*this};
  }

  bool operator<(const CommonRemoteFileLocation &other) const {
    return std::tie(id_) < std::tie(other.id_);
  }
  bool operator==(const CommonRemoteFileLocation &other) const {
    return std::tie(id_) == std::tie(other.id_);
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, const CommonRemoteFileLocation &location) {
  return string_builder << "[id = " << location.id_ << ", access_hash = " << location.access_hash_ << "]";
}

class FullRemoteFileLocation {
 public:
  FileType file_type_{FileType::None};

 private:
  static constexpr int32 WEB_LOCATION_FLAG = 1 << 24;
  static constexpr int32 FILE_REFERENCE_FLAG = 1 << 25;
  DcId dc_id_;
  string file_reference_;
  enum class LocationType : int32 { Web, Photo, Common, None };
  Variant<WebRemoteFileLocation, PhotoRemoteFileLocation, CommonRemoteFileLocation> variant_;

  LocationType location_type() const {
    if (is_web()) {
      return LocationType::Web;
    }
    switch (file_type_) {
      case FileType::Photo:
      case FileType::ProfilePhoto:
      case FileType::Thumbnail:
      case FileType::EncryptedThumbnail:
      case FileType::Wallpaper:
        return LocationType::Photo;
      case FileType::Video:
      case FileType::VoiceNote:
      case FileType::Document:
      case FileType::Sticker:
      case FileType::Audio:
      case FileType::Animation:
      case FileType::Encrypted:
      case FileType::VideoNote:
      case FileType::SecureRaw:
      case FileType::Secure:
      case FileType::Background:
      case FileType::DocumentAsFile:
        return LocationType::Common;
      case FileType::None:
      case FileType::Size:
      default:
        UNREACHABLE();
      case FileType::Temp:
        return LocationType::None;
    }
  }

  WebRemoteFileLocation &web() {
    return variant_.get<WebRemoteFileLocation>();
  }
  PhotoRemoteFileLocation &photo() {
    return variant_.get<PhotoRemoteFileLocation>();
  }
  CommonRemoteFileLocation &common() {
    return variant_.get<CommonRemoteFileLocation>();
  }
  const WebRemoteFileLocation &web() const {
    return variant_.get<WebRemoteFileLocation>();
  }
  const PhotoRemoteFileLocation &photo() const {
    return variant_.get<PhotoRemoteFileLocation>();
  }
  const CommonRemoteFileLocation &common() const {
    return variant_.get<CommonRemoteFileLocation>();
  }

  friend StringBuilder &operator<<(StringBuilder &string_builder,
                                   const FullRemoteFileLocation &full_remote_file_location);

  int32 key_type() const {
    auto type = static_cast<int32>(file_type_);
    if (is_web()) {
      type |= WEB_LOCATION_FLAG;
    }
    return type;
  }

  void check_file_reference() {
    if (file_reference_ == FileReferenceView::invalid_file_reference()) {
      LOG(ERROR) << "Tried to register file with invalid file reference";
      file_reference_.clear();
    }
  }

 public:
  template <class StorerT>
  void store(StorerT &storer) const;
  template <class ParserT>
  void parse(ParserT &parser);

  struct AsKey {
    const FullRemoteFileLocation &key;

    template <class StorerT>
    void store(StorerT &storer) const;
  };
  AsKey as_key() const {
    return AsKey{*this};
  }

  struct AsUnique {
    const FullRemoteFileLocation &key;

    template <class StorerT>
    void store(StorerT &storer) const;
  };
  AsUnique as_unique() const {
    return AsUnique{*this};
  }

  DcId get_dc_id() const {
    CHECK(!is_web());
    return dc_id_;
  }

  int64 get_access_hash() const {
    switch (location_type()) {
      case LocationType::Photo:
        return photo().access_hash_;
      case LocationType::Common:
        return common().access_hash_;
      case LocationType::Web:
        return web().access_hash_;
      case LocationType::None:
      default:
        UNREACHABLE();
        return 0;
    }
  }

  int64 get_id() const {
    switch (location_type()) {
      case LocationType::Photo:
        return photo().id_;
      case LocationType::Common:
        return common().id_;
      case LocationType::Web:
      case LocationType::None:
      default:
        UNREACHABLE();
        return 0;
    }
  }

  PhotoSizeSource get_source() const {
    switch (location_type()) {
      case LocationType::Photo:
        return photo().source_;
      case LocationType::Common:
      case LocationType::Web:
        return PhotoSizeSource(0);
      case LocationType::None:
      default:
        UNREACHABLE();
        return PhotoSizeSource(0);
    }
  }

  void set_source(PhotoSizeSource source) {
    CHECK(is_photo());
    file_type_ = source.get_file_type();
    photo().source_ = std::move(source);
  }

  bool delete_file_reference(Slice bad_file_reference) {
    if (file_reference_ != FileReferenceView::invalid_file_reference() && file_reference_ == bad_file_reference) {
      file_reference_ = FileReferenceView::invalid_file_reference().str();
      return true;
    }
    return false;
  }

  bool has_file_reference() const {
    return file_reference_ != FileReferenceView::invalid_file_reference();
  }

  Slice get_file_reference() const {
    return file_reference_;
  }

  string get_url() const {
    if (is_web()) {
      return web().url_;
    }

    return string();
  }

  bool is_web() const {
    return variant_.get_offset() == 0;
  }
  bool is_photo() const {
    return location_type() == LocationType::Photo;
  }
  bool is_common() const {
    return location_type() == LocationType::Common;
  }
  bool is_encrypted_secret() const {
    return file_type_ == FileType::Encrypted;
  }
  bool is_encrypted_secure() const {
    return file_type_ == FileType::Secure;
  }
  bool is_encrypted_any() const {
    return is_encrypted_secret() || is_encrypted_secure();
  }
  bool is_secure() const {
    return file_type_ == FileType::SecureRaw || file_type_ == FileType::Secure;
  }
  bool is_document() const {
    return is_common() && !is_secure() && !is_encrypted_secret();
  }

#define as_input_web_file_location() as_input_web_file_location_impl(__FILE__, __LINE__)
  tl_object_ptr<telegram_api::inputWebFileLocation> as_input_web_file_location_impl(const char *file, int line) const {
    LOG_CHECK(is_web()) << file << ' ' << line;
    return make_tl_object<telegram_api::inputWebFileLocation>(web().url_, web().access_hash_);
  }

  tl_object_ptr<telegram_api::InputFileLocation> as_input_file_location() const {
    switch (location_type()) {
      case LocationType::Photo:
        switch (photo().source_.get_type()) {
          case PhotoSizeSource::Type::Legacy:
            return make_tl_object<telegram_api::inputPhotoLegacyFileLocation>(
                photo().id_, photo().access_hash_, BufferSlice(file_reference_), photo().volume_id_, photo().local_id_,
                photo().source_.legacy().secret);
          case PhotoSizeSource::Type::Thumbnail: {
            auto &thumbnail = photo().source_.thumbnail();
            switch (thumbnail.file_type) {
              case FileType::Photo:
                return make_tl_object<telegram_api::inputPhotoFileLocation>(
                    photo().id_, photo().access_hash_, BufferSlice(file_reference_),
                    std::string(1, static_cast<char>(static_cast<uint8>(thumbnail.thumbnail_type))));
              case FileType::Thumbnail:
                return make_tl_object<telegram_api::inputDocumentFileLocation>(
                    photo().id_, photo().access_hash_, BufferSlice(file_reference_),
                    std::string(1, static_cast<char>(static_cast<uint8>(thumbnail.thumbnail_type))));
              default:
                UNREACHABLE();
                break;
            }
            break;
          }
          case PhotoSizeSource::Type::DialogPhotoSmall:
          case PhotoSizeSource::Type::DialogPhotoBig: {
            auto &dialog_photo = photo().source_.dialog_photo();
            bool is_big = photo().source_.get_type() == PhotoSizeSource::Type::DialogPhotoBig;
            return make_tl_object<telegram_api::inputPeerPhotoFileLocation>(
                is_big * telegram_api::inputPeerPhotoFileLocation::Flags::BIG_MASK, false /*ignored*/,
                dialog_photo.get_input_peer(), photo().volume_id_, photo().local_id_);
          }
          case PhotoSizeSource::Type::StickerSetThumbnail: {
            auto &sticker_set_thumbnail = photo().source_.sticker_set_thumbnail();
            return make_tl_object<telegram_api::inputStickerSetThumb>(sticker_set_thumbnail.get_input_sticker_set(),
                                                                      photo().volume_id_, photo().local_id_);
          }
          default:
            break;
        }
        UNREACHABLE();
        return nullptr;
      case LocationType::Common:
        if (is_encrypted_secret()) {
          return make_tl_object<telegram_api::inputEncryptedFileLocation>(common().id_, common().access_hash_);
        } else if (is_secure()) {
          return make_tl_object<telegram_api::inputSecureFileLocation>(common().id_, common().access_hash_);
        } else {
          return make_tl_object<telegram_api::inputDocumentFileLocation>(common().id_, common().access_hash_,
                                                                         BufferSlice(file_reference_), string());
        }
      case LocationType::Web:
      case LocationType::None:
      default:
        UNREACHABLE();
        return nullptr;
    }
  }

#define as_input_document() as_input_document_impl(__FILE__, __LINE__)
  tl_object_ptr<telegram_api::inputDocument> as_input_document_impl(const char *file, int line) const {
    LOG_CHECK(is_common()) << file << ' ' << line;
    LOG_CHECK(is_document()) << file << ' ' << line;
    return make_tl_object<telegram_api::inputDocument>(common().id_, common().access_hash_,
                                                       BufferSlice(file_reference_));
  }

#define as_input_photo() as_input_photo_impl(__FILE__, __LINE__)
  tl_object_ptr<telegram_api::inputPhoto> as_input_photo_impl(const char *file, int line) const {
    LOG_CHECK(is_photo()) << file << ' ' << line;
    return make_tl_object<telegram_api::inputPhoto>(photo().id_, photo().access_hash_, BufferSlice(file_reference_));
  }

  tl_object_ptr<telegram_api::inputEncryptedFile> as_input_encrypted_file() const {
    CHECK(is_encrypted_secret());
    return make_tl_object<telegram_api::inputEncryptedFile>(common().id_, common().access_hash_);
  }

#define as_input_secure_file() as_input_secure_file_impl(__FILE__, __LINE__)
  tl_object_ptr<telegram_api::inputSecureFile> as_input_secure_file_impl(const char *file, int line) const {
    LOG_CHECK(is_secure()) << file << ' ' << line;
    return make_tl_object<telegram_api::inputSecureFile>(common().id_, common().access_hash_);
  }

  // TODO: this constructor is just for immediate unserialize
  FullRemoteFileLocation() = default;

  // photo
  FullRemoteFileLocation(const PhotoSizeSource &source, int64 id, int64 access_hash, int32 local_id, int64 volume_id,
                         DcId dc_id, std::string file_reference)
      : file_type_(source.get_file_type())
      , dc_id_(dc_id)
      , file_reference_(std::move(file_reference))
      , variant_(PhotoRemoteFileLocation{id, access_hash, volume_id, local_id, source}) {
    CHECK(is_photo());
    check_file_reference();
  }

  // document
  FullRemoteFileLocation(FileType file_type, int64 id, int64 access_hash, DcId dc_id, std::string file_reference)
      : file_type_(file_type)
      , dc_id_(dc_id)
      , file_reference_(std::move(file_reference))
      , variant_(CommonRemoteFileLocation{id, access_hash}) {
    CHECK(is_common());
    check_file_reference();
  }

  // web document
  FullRemoteFileLocation(FileType file_type, string url, int64 access_hash)
      : file_type_(file_type), dc_id_(), variant_(WebRemoteFileLocation{std::move(url), access_hash}) {
    CHECK(is_web());
    CHECK(!web().url_.empty());
  }

  bool operator<(const FullRemoteFileLocation &other) const {
    if (key_type() != other.key_type()) {
      return key_type() < other.key_type();
    }
    if (dc_id_ != other.dc_id_) {
      return dc_id_ < other.dc_id_;
    }
    switch (location_type()) {
      case LocationType::Photo:
        return photo() < other.photo();
      case LocationType::Common:
        return common() < other.common();
      case LocationType::Web:
        return web() < other.web();
      case LocationType::None:
      default:
        UNREACHABLE();
        return false;
    }
  }
  bool operator==(const FullRemoteFileLocation &other) const {
    if (key_type() != other.key_type()) {
      return false;
    }
    if (dc_id_ != other.dc_id_) {
      return false;
    }
    switch (location_type()) {
      case LocationType::Photo:
        return photo() == other.photo();
      case LocationType::Common:
        return common() == other.common();
      case LocationType::Web:
        return web() == other.web();
      case LocationType::None:
      default:
        UNREACHABLE();
        return false;
    }
  }

  static const int32 KEY_MAGIC = 0x64374632;
};

inline StringBuilder &operator<<(StringBuilder &string_builder,
                                 const FullRemoteFileLocation &full_remote_file_location) {
  string_builder << "[" << full_remote_file_location.file_type_;
  if (!full_remote_file_location.is_web()) {
    string_builder << ", " << full_remote_file_location.get_dc_id();
  }
  if (!full_remote_file_location.file_reference_.empty()) {
    string_builder << ", " << tag("file_reference", base64_encode(full_remote_file_location.file_reference_));
  }

  string_builder << ", location = ";
  if (full_remote_file_location.is_web()) {
    string_builder << full_remote_file_location.web();
  } else if (full_remote_file_location.is_photo()) {
    string_builder << full_remote_file_location.photo();
  } else if (full_remote_file_location.is_common()) {
    string_builder << full_remote_file_location.common();
  }

  return string_builder << "]";
}

class RemoteFileLocation {
 public:
  enum class Type : int32 { Empty, Partial, Full };

  Type type() const {
    return static_cast<Type>(variant_.get_offset());
  }

  PartialRemoteFileLocation &partial() {
    return variant_.get<1>();
  }
  FullRemoteFileLocation &full() {
    return variant_.get<2>();
  }
  const PartialRemoteFileLocation &partial() const {
    return variant_.get<1>();
  }
  const FullRemoteFileLocation &full() const {
    return variant_.get<2>();
  }

  template <class StorerT>
  void store(StorerT &storer) const;
  template <class ParserT>
  void parse(ParserT &parser);

  RemoteFileLocation() : variant_{EmptyRemoteFileLocation{}} {
  }
  explicit RemoteFileLocation(const FullRemoteFileLocation &full) : variant_(full) {
  }
  explicit RemoteFileLocation(const PartialRemoteFileLocation &partial) : variant_(partial) {
  }

 private:
  Variant<EmptyRemoteFileLocation, PartialRemoteFileLocation, FullRemoteFileLocation> variant_;

  friend bool operator==(const RemoteFileLocation &lhs, const RemoteFileLocation &rhs);

  bool is_empty() const {
    switch (type()) {
      case Type::Empty:
        return true;
      case Type::Partial:
        return partial().ready_part_count_ == 0;
      case Type::Full:
        return false;
      default:
        UNREACHABLE();
        return false;
    }
  }
};

inline bool operator==(const RemoteFileLocation &lhs, const RemoteFileLocation &rhs) {
  if (lhs.is_empty() && rhs.is_empty()) {
    return true;
  }
  return lhs.variant_ == rhs.variant_;
}

inline bool operator!=(const RemoteFileLocation &lhs, const RemoteFileLocation &rhs) {
  return !(lhs == rhs);
}

struct EmptyLocalFileLocation {
  template <class StorerT>
  void store(StorerT &storer) const {
  }
  template <class ParserT>
  void parse(ParserT &parser) {
  }
};

inline bool operator==(const EmptyLocalFileLocation &lhs, const EmptyLocalFileLocation &rhs) {
  return true;
}

inline bool operator!=(const EmptyLocalFileLocation &lhs, const EmptyLocalFileLocation &rhs) {
  return !(lhs == rhs);
}

struct PartialLocalFileLocation {
  FileType file_type_;
  int32 part_size_;
  string path_;
  string iv_;
  string ready_bitmask_;

  template <class StorerT>
  void store(StorerT &storer) const;
  template <class ParserT>
  void parse(ParserT &parser);
};

inline bool operator==(const PartialLocalFileLocation &lhs, const PartialLocalFileLocation &rhs) {
  return lhs.file_type_ == rhs.file_type_ && lhs.path_ == rhs.path_ && lhs.part_size_ == rhs.part_size_ &&
         lhs.iv_ == rhs.iv_ && lhs.ready_bitmask_ == rhs.ready_bitmask_;
}

inline bool operator!=(const PartialLocalFileLocation &lhs, const PartialLocalFileLocation &rhs) {
  return !(lhs == rhs);
}

inline StringBuilder &operator<<(StringBuilder &sb, const PartialLocalFileLocation &location) {
  return sb << "[partial local location of " << location.file_type_ << " with part size " << location.part_size_
            << " and ready parts " << Bitmask(Bitmask::Decode{}, location.ready_bitmask_) << "] at \"" << location.path_
            << '"';
}

struct FullLocalFileLocation {
  FileType file_type_;
  string path_;
  uint64 mtime_nsec_;

  template <class StorerT>
  void store(StorerT &storer) const;
  template <class ParserT>
  void parse(ParserT &parser);

  const FullLocalFileLocation &as_key() const {
    return *this;
  }

  // TODO: remove this constructor
  FullLocalFileLocation() : file_type_(FileType::Photo) {
  }
  FullLocalFileLocation(FileType file_type, string path, uint64 mtime_nsec)
      : file_type_(file_type), path_(std::move(path)), mtime_nsec_(mtime_nsec) {
  }

  static const int32 KEY_MAGIC = 0x84373817;
};

inline bool operator<(const FullLocalFileLocation &lhs, const FullLocalFileLocation &rhs) {
  return std::tie(lhs.file_type_, lhs.mtime_nsec_, lhs.path_) < std::tie(rhs.file_type_, rhs.mtime_nsec_, rhs.path_);
}

inline bool operator==(const FullLocalFileLocation &lhs, const FullLocalFileLocation &rhs) {
  return std::tie(lhs.file_type_, lhs.mtime_nsec_, lhs.path_) == std::tie(rhs.file_type_, rhs.mtime_nsec_, rhs.path_);
}

inline bool operator!=(const FullLocalFileLocation &lhs, const FullLocalFileLocation &rhs) {
  return !(lhs == rhs);
}

inline StringBuilder &operator<<(StringBuilder &sb, const FullLocalFileLocation &location) {
  return sb << "[full local location of " << location.file_type_ << "] at \"" << location.path_ << '"';
}

struct PartialLocalFileLocationPtr {
  unique_ptr<PartialLocalFileLocation> location_;  // must never be equal to nullptr

  PartialLocalFileLocationPtr() : location_(make_unique<PartialLocalFileLocation>()) {
  }
  explicit PartialLocalFileLocationPtr(PartialLocalFileLocation location)
      : location_(make_unique<PartialLocalFileLocation>(location)) {
  }
  PartialLocalFileLocationPtr(const PartialLocalFileLocationPtr &other)
      : location_(make_unique<PartialLocalFileLocation>(*other.location_)) {
  }
  PartialLocalFileLocationPtr &operator=(const PartialLocalFileLocationPtr &other) {
    *location_ = *other.location_;
    return *this;
  }
  PartialLocalFileLocationPtr(PartialLocalFileLocationPtr &&other)
      : location_(make_unique<PartialLocalFileLocation>(std::move(*other.location_))) {
  }
  PartialLocalFileLocationPtr &operator=(PartialLocalFileLocationPtr &&other) {
    *location_ = std::move(*other.location_);
    return *this;
  }
  ~PartialLocalFileLocationPtr() = default;

  template <class StorerT>
  void store(StorerT &storer) const;
  template <class ParserT>
  void parse(ParserT &parser);
};

inline bool operator==(const PartialLocalFileLocationPtr &lhs, const PartialLocalFileLocationPtr &rhs) {
  return *lhs.location_ == *rhs.location_;
}

class LocalFileLocation {
 public:
  enum class Type : int32 { Empty, Partial, Full };

  Type type() const {
    return static_cast<Type>(variant_.get_offset());
  }

  PartialLocalFileLocation &partial() {
    return *variant_.get<1>().location_;
  }
  FullLocalFileLocation &full() {
    return variant_.get<2>();
  }
  const PartialLocalFileLocation &partial() const {
    return *variant_.get<1>().location_;
  }
  const FullLocalFileLocation &full() const {
    return variant_.get<2>();
  }

  CSlice file_name() const {
    switch (type()) {
      case Type::Partial:
        return partial().path_;
      case Type::Full:
        return full().path_;
      case Type::Empty:
      default:
        return CSlice();
    }
  }

  template <class StorerT>
  void store(StorerT &storer) const;
  template <class ParserT>
  void parse(ParserT &parser);

  LocalFileLocation() : variant_{EmptyLocalFileLocation()} {
  }
  explicit LocalFileLocation(const PartialLocalFileLocation &partial) : variant_(PartialLocalFileLocationPtr(partial)) {
  }
  explicit LocalFileLocation(const FullLocalFileLocation &full) : variant_(full) {
  }
  LocalFileLocation(FileType file_type, string path, uint64 mtime_nsec)
      : variant_(FullLocalFileLocation{file_type, std::move(path), mtime_nsec}) {
  }

 private:
  Variant<EmptyLocalFileLocation, PartialLocalFileLocationPtr, FullLocalFileLocation> variant_;

  friend bool operator==(const LocalFileLocation &lhs, const LocalFileLocation &rhs);
};

inline bool operator==(const LocalFileLocation &lhs, const LocalFileLocation &rhs) {
  return lhs.variant_ == rhs.variant_;
}

inline bool operator!=(const LocalFileLocation &lhs, const LocalFileLocation &rhs) {
  return !(lhs == rhs);
}

inline StringBuilder &operator<<(StringBuilder &sb, const LocalFileLocation &location) {
  switch (location.type()) {
    case LocalFileLocation::Type::Empty:
      return sb << "[empty local location]";
    case LocalFileLocation::Type::Partial:
      return sb << location.partial();
    case LocalFileLocation::Type::Full:
      return sb << location.full();
    default:
      UNREACHABLE();
      return sb;
  }
}

struct FullGenerateFileLocation {
  FileType file_type_{FileType::None};
  string original_path_;
  string conversion_;
  static const int32 KEY_MAGIC = 0x8b60a1c8;

  template <class StorerT>
  void store(StorerT &storer) const;
  template <class ParserT>
  void parse(ParserT &parser);

  const FullGenerateFileLocation &as_key() const {
    return *this;
  }
  FullGenerateFileLocation() = default;
  FullGenerateFileLocation(FileType file_type, string original_path, string conversion)
      : file_type_(file_type), original_path_(std::move(original_path)), conversion_(std::move(conversion)) {
  }
};

inline bool operator<(const FullGenerateFileLocation &lhs, const FullGenerateFileLocation &rhs) {
  return std::tie(lhs.file_type_, lhs.original_path_, lhs.conversion_) <
         std::tie(rhs.file_type_, rhs.original_path_, rhs.conversion_);
}

inline bool operator==(const FullGenerateFileLocation &lhs, const FullGenerateFileLocation &rhs) {
  return std::tie(lhs.file_type_, lhs.original_path_, lhs.conversion_) ==
         std::tie(rhs.file_type_, rhs.original_path_, rhs.conversion_);
}

inline bool operator!=(const FullGenerateFileLocation &lhs, const FullGenerateFileLocation &rhs) {
  return !(lhs == rhs);
}

inline StringBuilder &operator<<(StringBuilder &string_builder,
                                 const FullGenerateFileLocation &full_generated_file_location) {
  return string_builder << '[' << tag("file_type", full_generated_file_location.file_type_)
                        << tag("original_path", full_generated_file_location.original_path_)
                        << tag("conversion", full_generated_file_location.conversion_) << ']';
}

class GenerateFileLocation {
 public:
  enum class Type : int32 { Empty, Full };

  Type type() const {
    return type_;
  }

  FullGenerateFileLocation &full() {
    CHECK(type_ == Type::Full);
    return full_;
  }
  const FullGenerateFileLocation &full() const {
    CHECK(type_ == Type::Full);
    return full_;
  }

  template <class StorerT>
  void store(StorerT &storer) const;
  template <class ParserT>
  void parse(ParserT &parser);

  GenerateFileLocation() : type_(Type::Empty) {
  }

  explicit GenerateFileLocation(const FullGenerateFileLocation &full) : type_(Type::Full), full_(full) {
  }

  GenerateFileLocation(FileType file_type, string original_path, string conversion)
      : type_(Type::Full), full_{file_type, std::move(original_path), std::move(conversion)} {
  }

 private:
  Type type_;
  FullGenerateFileLocation full_;
};

inline bool operator==(const GenerateFileLocation &lhs, const GenerateFileLocation &rhs) {
  if (lhs.type() != rhs.type()) {
    return false;
  }
  switch (lhs.type()) {
    case GenerateFileLocation::Type::Empty:
      return true;
    case GenerateFileLocation::Type::Full:
      return lhs.full() == rhs.full();
  }
  UNREACHABLE();
  return false;
}

inline bool operator!=(const GenerateFileLocation &lhs, const GenerateFileLocation &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
