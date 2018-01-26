//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/DialogId.h"
#include "td/telegram/net/DcId.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/format.h"
#include "td/utils/int_types.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tl_helpers.h"
#include "td/utils/tl_storers.h"
#include "td/utils/Variant.h"

#include <cstring>
#include <tuple>

namespace td {

enum class FileType : int8 {
  Thumbnail,
  ProfilePhoto,
  Photo,
  VoiceNote,
  Video,
  Document,
  Encrypted,
  Temp,
  Sticker,
  Audio,
  Animation,
  EncryptedThumbnail,
  Wallpaper,
  VideoNote,
  Size,
  None
};

inline FileType from_td_api(const td_api::FileType &file_type) {
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
      return FileType::Wallpaper;
    case td_api::fileTypeVideoNote::ID:
      return FileType::VideoNote;
    case td_api::fileTypeNone::ID:
      return FileType::None;
    default:
      UNREACHABLE();
      return FileType::None;
  }
}

inline tl_object_ptr<td_api::FileType> as_td_api(FileType file_type) {
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
    case FileType::None:
      return make_tl_object<td_api::fileTypeNone>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

enum class FileDirType : int8 { Secure, Common };
inline FileDirType get_file_dir_type(FileType file_type) {
  switch (file_type) {
    case FileType::Thumbnail:
    case FileType::ProfilePhoto:
    case FileType::Encrypted:
    case FileType::Sticker:
    case FileType::Temp:
    case FileType::Wallpaper:
    case FileType::EncryptedThumbnail:
      return FileDirType::Secure;
    default:
      return FileDirType::Common;
  }
}

constexpr int32 file_type_size = static_cast<int32>(FileType::Size);
extern const char *file_type_name[file_type_size];

struct FileEncryptionKey {
  FileEncryptionKey() = default;
  FileEncryptionKey(Slice key, Slice iv) : key_iv_(key.size() + iv.size(), '\0') {
    if (key.size() != 32 || iv.size() != 32) {
      LOG(ERROR) << "Wrong key/iv sizes: " << key.size() << " " << iv.size();
      return;
    }
    CHECK(key_iv_.size() == 64);
    std::memcpy(&key_iv_[0], key.data(), key.size());
    std::memcpy(&key_iv_[key.size()], iv.data(), iv.size());
  }
  static FileEncryptionKey create() {
    FileEncryptionKey res;
    res.key_iv_.resize(64);
    Random::secure_bytes(res.key_iv_);
    return res;
  }

  const UInt256 &key() const {
    CHECK(key_iv_.size() == 64);
    return *reinterpret_cast<const UInt256 *>(key_iv_.data());
  }
  Slice key_slice() const {
    CHECK(key_iv_.size() == 64);
    return Slice(key_iv_.data(), 32);
  }

  UInt256 &mutable_iv() {
    CHECK(key_iv_.size() == 64);
    return *reinterpret_cast<UInt256 *>(&key_iv_[0] + 32);
  }
  Slice iv_slice() const {
    CHECK(key_iv_.size() == 64);
    return Slice(key_iv_.data() + 32, 32);
  }

  int32 calc_fingerprint() const {
    char buf[16];
    md5(key_iv_, {buf, sizeof(buf)});
    return as<int32>(buf) ^ as<int32>(buf + 4);
  }

  bool empty() const {
    return key_iv_.empty();
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    td::store(key_iv_, storer);
  }
  template <class ParserT>
  void parse(ParserT &parser) {
    td::parse(key_iv_, parser);
  }

  string key_iv_;  // TODO wrong alignment is possible
};

inline bool operator==(const FileEncryptionKey &lhs, const FileEncryptionKey &rhs) {
  return lhs.key_iv_ == rhs.key_iv_;
}

inline bool operator!=(const FileEncryptionKey &lhs, const FileEncryptionKey &rhs) {
  return !(lhs == rhs);
}

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
  void store(StorerT &storer) const {
    using td::store;
    store(file_id_, storer);
    store(part_count_, storer);
    store(part_size_, storer);
    store(ready_part_count_, storer);
    store(is_big_, storer);
  }
  template <class ParserT>
  void parse(ParserT &parser) {
    using td::parse;
    parse(file_id_, parser);
    parse(part_count_, parser);
    parse(part_size_, parser);
    parse(ready_part_count_, parser);
    parse(is_big_, parser);
  }
};

inline bool operator==(const PartialRemoteFileLocation &lhs, const PartialRemoteFileLocation &rhs) {
  return lhs.file_id_ == rhs.file_id_ && lhs.part_count_ == rhs.part_count_ && lhs.part_size_ == rhs.part_size_ &&
         lhs.ready_part_count_ == rhs.ready_part_count_ && lhs.is_big_ == rhs.is_big_;
}

inline bool operator!=(const PartialRemoteFileLocation &lhs, const PartialRemoteFileLocation &rhs) {
  return !(lhs == rhs);
}

struct PhotoRemoteFileLocation {
  int64 id_;
  int64 access_hash_;
  int64 volume_id_;
  int64 secret_;
  int32 local_id_;

  template <class StorerT>
  void store(StorerT &storer) const {
    using td::store;
    store(id_, storer);
    store(access_hash_, storer);
    store(volume_id_, storer);
    store(secret_, storer);
    store(local_id_, storer);
  }
  template <class ParserT>
  void parse(ParserT &parser) {
    using td::parse;
    parse(id_, parser);
    parse(access_hash_, parser);
    parse(volume_id_, parser);
    parse(secret_, parser);
    parse(local_id_, parser);
  }
  struct AsKey {
    const PhotoRemoteFileLocation &key;
    template <class StorerT>
    void store(StorerT &storer) const {
      using td::store;
      store(key.id_, storer);
      store(key.volume_id_, storer);
      store(key.local_id_, storer);
    }
  };
  AsKey as_key() const {
    return AsKey{*this};
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
                        << ", volume_id = " << location.volume_id_ << ", secret = " << location.secret_
                        << ", local_id = " << location.local_id_ << "]";
}

struct WebRemoteFileLocation {
  string url_;
  int64 access_hash_;

  template <class StorerT>
  void store(StorerT &storer) const {
    using td::store;
    store(url_, storer);
    store(access_hash_, storer);
  }
  template <class ParserT>
  void parse(ParserT &parser) {
    using td::parse;
    parse(url_, parser);
    parse(access_hash_, parser);
  }
  struct AsKey {
    const WebRemoteFileLocation &key;
    template <class StorerT>
    void store(StorerT &storer) const {
      using td::store;
      store(key.url_, storer);
    }
  };
  AsKey as_key() const {
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
  void store(StorerT &storer) const {
    using td::store;
    store(id_, storer);
    store(access_hash_, storer);
  }
  template <class ParserT>
  void parse(ParserT &parser) {
    using td::parse;
    parse(id_, parser);
    parse(access_hash_, parser);
  }
  struct AsKey {
    const CommonRemoteFileLocation &key;
    template <class StorerT>
    void store(StorerT &storer) const {
      td::store(key.id_, storer);
    }
  };
  AsKey as_key() const {
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
  bool web_location_flag_{false};
  DcId dc_id_;
  enum class LocationType { Web, Photo, Common, None };
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

  int32 full_type() const {
    auto type = static_cast<int32>(file_type_);
    if (is_web()) {
      type |= WEB_LOCATION_FLAG;
    }
    return type;
  }

 public:
  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    store(full_type(), storer);
    store(dc_id_.get_value(), storer);
    variant_.visit([&](auto &&value) {
      using td::store;
      store(value, storer);
    });
  }
  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    int32 raw_type;
    parse(raw_type, parser);
    web_location_flag_ = (raw_type & WEB_LOCATION_FLAG) != 0;
    raw_type &= ~WEB_LOCATION_FLAG;
    if (raw_type < 0 || raw_type >= static_cast<int32>(FileType::Size)) {
      return parser.set_error("Invalid FileType in FullRemoteFileLocation");
    }
    file_type_ = static_cast<FileType>(raw_type);
    int32 dc_id_value;
    parse(dc_id_value, parser);
    dc_id_ = DcId::from_value(dc_id_value);

    switch (location_type()) {
      case LocationType::Web: {
        variant_ = WebRemoteFileLocation();
        return web().parse(parser);
      }
      case LocationType::Photo: {
        variant_ = PhotoRemoteFileLocation();
        return photo().parse(parser);
      }
      case LocationType::Common: {
        variant_ = CommonRemoteFileLocation();
        return common().parse(parser);
      }
      case LocationType::None: {
        break;
      }
    }
    parser.set_error("Invalid FileType in FullRemoteFileLocation");
  }

  struct AsKey {
    const FullRemoteFileLocation &key;
    template <class StorerT>
    void store(StorerT &storer) const {
      using td::store;
      store(key.full_type(), storer);
      key.variant_.visit([&](auto &&value) {
        using td::store;
        store(value.as_key(), storer);
      });
    }
  };
  AsKey as_key() const {
    return AsKey{*this};
  }

  DcId get_dc_id() const {
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
  string get_url() const {
    if (is_web()) {
      return web().url_;
    }

    return string();
  }

  bool is_web() const {
    return web_location_flag_;
  }
  bool is_photo() const {
    return location_type() == LocationType::Photo;
  }
  bool is_common() const {
    return location_type() == LocationType::Common;
  }
  bool is_encrypted() const {
    return file_type_ == FileType::Encrypted;
  }

  tl_object_ptr<telegram_api::inputWebFileLocation> as_input_web_file_location() const {
    CHECK(is_web());
    return make_tl_object<telegram_api::inputWebFileLocation>(web().url_, web().access_hash_);
  }
  tl_object_ptr<telegram_api::InputFileLocation> as_input_file_location() const {
    switch (location_type()) {
      case LocationType::Photo:
        return make_tl_object<telegram_api::inputFileLocation>(photo().volume_id_, photo().local_id_, photo().secret_);
      case LocationType::Common:
        if (is_encrypted()) {
          return make_tl_object<telegram_api::inputEncryptedFileLocation>(common().id_, common().access_hash_);
        } else {
          return make_tl_object<telegram_api::inputDocumentFileLocation>(common().id_, common().access_hash_, 0);
        }
      case LocationType::Web:
      case LocationType::None:
      default:
        UNREACHABLE();
        return nullptr;
    }
  }

  tl_object_ptr<telegram_api::InputDocument> as_input_document() const {
    CHECK(is_common());
    LOG_IF(ERROR, is_encrypted()) << "Can't call as_input_document on an encrypted file";
    return make_tl_object<telegram_api::inputDocument>(common().id_, common().access_hash_);
  }

  tl_object_ptr<telegram_api::InputPhoto> as_input_photo() const {
    CHECK(is_photo());
    return make_tl_object<telegram_api::inputPhoto>(photo().id_, photo().access_hash_);
  }

  tl_object_ptr<telegram_api::InputEncryptedFile> as_input_encrypted_file() const {
    CHECK(is_encrypted()) << "Can't call as_input_encrypted_file on a non-encrypted file";
    return make_tl_object<telegram_api::inputEncryptedFile>(common().id_, common().access_hash_);
  }

  // TODO: this constructor is just for immediate unserialize
  FullRemoteFileLocation() = default;
  FullRemoteFileLocation(FileType file_type, int64 id, int64 access_hash, int32 local_id, int64 volume_id, int64 secret,
                         DcId dc_id)
      : file_type_(file_type)
      , dc_id_(dc_id)
      , variant_(PhotoRemoteFileLocation{id, access_hash, volume_id, secret, local_id}) {
    CHECK(is_photo());
  }
  FullRemoteFileLocation(FileType file_type, int64 id, int64 access_hash, DcId dc_id)
      : file_type_(file_type), dc_id_(dc_id), variant_(CommonRemoteFileLocation{id, access_hash}) {
    CHECK(is_common());
  }
  FullRemoteFileLocation(FileType file_type, string url, int64 access_hash, DcId dc_id)
      : file_type_(file_type)
      , web_location_flag_{true}
      , dc_id_(dc_id)
      , variant_(WebRemoteFileLocation{std::move(url), access_hash}) {
    CHECK(is_web());
    CHECK(!web().url_.empty());
  }

  bool operator<(const FullRemoteFileLocation &other) const {
    if (full_type() != other.full_type()) {
      return full_type() < other.full_type();
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
    if (full_type() != other.full_type()) {
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
  string_builder << "[" << file_type_name[static_cast<int32>(full_remote_file_location.file_type_)] << ", "
                 << full_remote_file_location.get_dc_id() << ", location = ";

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

  template <class StorerT>
  void store(StorerT &storer) const {
    storer.store_int(variant_.get_offset());
    bool ok{false};
    variant_.visit([&](auto &&value) {
      using td::store;
      store(value, storer);
      ok = true;
    });
    CHECK(ok);
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
  template <class ParserT>
  void parse(ParserT &parser) {
    auto type = static_cast<Type>(parser.fetch_int());
    switch (type) {
      case Type::Empty: {
        variant_ = EmptyRemoteFileLocation();
        return;
      }
      case Type::Partial: {
        variant_ = PartialRemoteFileLocation();
        return partial().parse(parser);
      }
      case Type::Full: {
        variant_ = FullRemoteFileLocation();
        return full().parse(parser);
      }
    }
    parser.set_error("Invalid type in RemoteFileLocation");
  }

  RemoteFileLocation() : variant_{EmptyRemoteFileLocation{}} {
  }
  explicit RemoteFileLocation(const FullRemoteFileLocation &full) : variant_(full) {
  }
  explicit RemoteFileLocation(const PartialRemoteFileLocation &partial) : variant_(partial) {
  }
  RemoteFileLocation(FileType file_type, int64 id, int64 access_hash, int32 local_id, int64 volume_id, int64 secret,
                     DcId dc_id)
      : variant_(FullRemoteFileLocation{file_type, id, access_hash, local_id, volume_id, secret, dc_id}) {
  }
  RemoteFileLocation(FileType file_type, int64 id, int64 access_hash, DcId dc_id)
      : variant_(FullRemoteFileLocation{file_type, id, access_hash, dc_id}) {
  }

 private:
  Variant<EmptyRemoteFileLocation, PartialRemoteFileLocation, FullRemoteFileLocation> variant_;

  friend bool operator==(const RemoteFileLocation &lhs, const RemoteFileLocation &rhs);
};

inline bool operator==(const RemoteFileLocation &lhs, const RemoteFileLocation &rhs) {
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
  string path_;
  int32 part_size_;
  int32 ready_part_count_;
  string iv_;

  template <class StorerT>
  void store(StorerT &storer) const {
    using td::store;
    store(file_type_, storer);
    store(path_, storer);
    store(part_size_, storer);
    store(ready_part_count_, storer);
    store(iv_, storer);
  }
  template <class ParserT>
  void parse(ParserT &parser) {
    using td::parse;
    parse(file_type_, parser);
    if (file_type_ < FileType::Thumbnail || file_type_ >= FileType::Size) {
      return parser.set_error("Invalid type in PartialLocalFileLocation");
    }
    parse(path_, parser);
    parse(part_size_, parser);
    parse(ready_part_count_, parser);
    parse(iv_, parser);
  }
};

inline bool operator==(const PartialLocalFileLocation &lhs, const PartialLocalFileLocation &rhs) {
  return lhs.file_type_ == rhs.file_type_ && lhs.path_ == rhs.path_ && lhs.part_size_ == rhs.part_size_ &&
         lhs.ready_part_count_ == rhs.ready_part_count_ && lhs.iv_ == rhs.iv_;
}

inline bool operator!=(const PartialLocalFileLocation &lhs, const PartialLocalFileLocation &rhs) {
  return !(lhs == rhs);
}

struct FullLocalFileLocation {
  FileType file_type_;
  string path_;
  uint64 mtime_nsec_;

  template <class StorerT>
  void store(StorerT &storer) const {
    using td::store;
    store(file_type_, storer);
    store(mtime_nsec_, storer);
    store(path_, storer);
  }
  template <class ParserT>
  void parse(ParserT &parser) {
    using td::parse;
    parse(file_type_, parser);
    if (file_type_ < FileType::Thumbnail || file_type_ >= FileType::Size) {
      return parser.set_error("Invalid type in FullLocalFileLocation");
    }
    parse(mtime_nsec_, parser);
    parse(path_, parser);
  }
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
  return sb << tag("path", location.path_);
}

class LocalFileLocation {
 public:
  enum class Type : int32 { Empty, Partial, Full };

  Type type() const {
    return static_cast<Type>(variant_.get_offset());
  }

  PartialLocalFileLocation &partial() {
    return variant_.get<1>();
  }
  FullLocalFileLocation &full() {
    return variant_.get<2>();
  }
  const PartialLocalFileLocation &partial() const {
    return variant_.get<1>();
  }
  const FullLocalFileLocation &full() const {
    return variant_.get<2>();
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using td::store;
    store(variant_.get_offset(), storer);
    variant_.visit([&](auto &&value) {
      using td::store;
      store(value, storer);
    });
  }
  template <class ParserT>
  void parse(ParserT &parser) {
    using td::parse;
    auto type = static_cast<Type>(parser.fetch_int());
    switch (type) {
      case Type::Empty:
        variant_ = EmptyLocalFileLocation();
        return;
      case Type::Partial:
        variant_ = PartialLocalFileLocation();
        return parse(partial(), parser);
      case Type::Full:
        variant_ = FullLocalFileLocation();
        return parse(full(), parser);
    }
    return parser.set_error("Invalid type in LocalFileLocation");
  }

  LocalFileLocation() : variant_{EmptyLocalFileLocation()} {
  }
  explicit LocalFileLocation(const PartialLocalFileLocation &partial) : variant_(partial) {
  }
  explicit LocalFileLocation(const FullLocalFileLocation &full) : variant_(full) {
  }
  LocalFileLocation(FileType file_type, string path, uint64 mtime_nsec)
      : variant_(FullLocalFileLocation{file_type, std::move(path), mtime_nsec}) {
  }

 private:
  Variant<EmptyLocalFileLocation, PartialLocalFileLocation, FullLocalFileLocation> variant_;

  friend bool operator==(const LocalFileLocation &lhs, const LocalFileLocation &rhs);
};

inline bool operator==(const LocalFileLocation &lhs, const LocalFileLocation &rhs) {
  return lhs.variant_ == rhs.variant_;
}

inline bool operator!=(const LocalFileLocation &lhs, const LocalFileLocation &rhs) {
  return !(lhs == rhs);
}

struct FullGenerateFileLocation {
  FileType file_type_{FileType::None};
  string original_path_;
  string conversion_;
  static const int32 KEY_MAGIC = 0x8b60a1c8;

  template <class StorerT>
  void store(StorerT &storer) const {
    using td::store;
    store(file_type_, storer);
    store(original_path_, storer);
    store(conversion_, storer);
  }
  template <class ParserT>
  void parse(ParserT &parser) {
    using td::parse;
    parse(file_type_, parser);
    parse(original_path_, parser);
    parse(conversion_, parser);
  }

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
  return string_builder << "["
                        << tag("file_type", file_type_name[static_cast<int32>(full_generated_file_location.file_type_)])
                        << tag("original_path", full_generated_file_location.original_path_)
                        << tag("conversion", full_generated_file_location.conversion_) << "]";
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
  void store(StorerT &storer) const {
    td::store(type_, storer);
    switch (type_) {
      case Type::Empty:
        return;
      case Type::Full:
        return td::store(full_, storer);
    }
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    td::parse(type_, parser);
    switch (type_) {
      case Type::Empty:
        return;
      case Type::Full:
        return td::parse(full_, parser);
    }
    return parser.set_error("Invalid type in GenerateFileLocation");
  }

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

class FileData {
 public:
  DialogId owner_dialog_id_;
  uint64 pmc_id_ = 0;
  RemoteFileLocation remote_;
  LocalFileLocation local_;
  GenerateFileLocation generate_;
  int64 size_ = 0;
  int64 expected_size_ = 0;
  string name_;
  string url_;
  FileEncryptionKey encryption_key_;

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    bool has_owner_dialog_id = owner_dialog_id_.is_valid();
    bool has_expected_size = size_ == 0 && expected_size_ != 0;
    BEGIN_STORE_FLAGS();
    STORE_FLAG(has_owner_dialog_id);
    STORE_FLAG(has_expected_size);
    END_STORE_FLAGS();

    if (has_owner_dialog_id) {
      store(owner_dialog_id_, storer);
    }
    store(pmc_id_, storer);
    store(remote_, storer);
    store(local_, storer);
    store(generate_, storer);
    if (has_expected_size) {
      store(expected_size_, storer);
    } else {
      store(size_, storer);
    }
    store(name_, storer);
    store(url_, storer);
    store(encryption_key_, storer);
  }
  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    bool has_owner_dialog_id;
    bool has_expected_size;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(has_owner_dialog_id);
    PARSE_FLAG(has_expected_size);
    END_PARSE_FLAGS();

    if (has_owner_dialog_id) {
      parse(owner_dialog_id_, parser);
    }
    parse(pmc_id_, parser);
    parse(remote_, parser);
    parse(local_, parser);
    parse(generate_, parser);
    if (has_expected_size) {
      parse(expected_size_, parser);
    } else {
      parse(size_, parser);
    }
    parse(name_, parser);
    parse(url_, parser);
    parse(encryption_key_, parser);
  }
};
inline StringBuilder &operator<<(StringBuilder &sb, const FileData &file_data) {
  sb << "[" << tag("name", file_data.name_) << " " << file_data.owner_dialog_id_ << " " << tag("size", file_data.size_)
     << tag("expected_size", file_data.expected_size_);
  if (!file_data.url_.empty()) {
    sb << tag("url", file_data.url_);
  }
  if (file_data.local_.type() == LocalFileLocation::Type::Full) {
    sb << " local " << file_data.local_.full();
  }
  if (file_data.generate_.type() == GenerateFileLocation::Type::Full) {
    sb << " generate " << file_data.generate_.full();
  }
  if (file_data.remote_.type() == RemoteFileLocation::Type::Full) {
    sb << " remote " << file_data.remote_.full();
  }
  return sb << "]";
}

template <class T>
string as_key(const T &object) {
  TlStorerCalcLength calc_length;
  calc_length.store_int(0);
  object.as_key().store(calc_length);

  BufferSlice key_buffer{calc_length.get_length()};
  auto key = key_buffer.as_slice();
  TlStorerUnsafe storer(key.begin());
  storer.store_int(T::KEY_MAGIC);
  object.as_key().store(storer);
  return key.str();
}

}  // namespace td
