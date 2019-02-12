//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/files/FileLocation.h"

#include "td/telegram/files/FileType.h"
#include "td/telegram/net/DcId.h"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"
#include "td/utils/Variant.h"

namespace td {

template <class StorerT>
void PartialRemoteFileLocation::store(StorerT &storer) const {
  using td::store;
  store(file_id_, storer);
  store(part_count_, storer);
  store(part_size_, storer);
  store(ready_part_count_, storer);
  store(is_big_, storer);
}

template <class ParserT>
void PartialRemoteFileLocation::parse(ParserT &parser) {
  using td::parse;
  parse(file_id_, parser);
  parse(part_count_, parser);
  parse(part_size_, parser);
  parse(ready_part_count_, parser);
  parse(is_big_, parser);
}

template <class StorerT>
void PhotoRemoteFileLocation::store(StorerT &storer) const {
  using td::store;
  store(id_, storer);
  store(access_hash_, storer);
  store(volume_id_, storer);
  store(secret_, storer);
  store(local_id_, storer);
}

template <class ParserT>
void PhotoRemoteFileLocation::parse(ParserT &parser) {
  using td::parse;
  parse(id_, parser);
  parse(access_hash_, parser);
  parse(volume_id_, parser);
  parse(secret_, parser);
  parse(local_id_, parser);
}

template <class StorerT>
void PhotoRemoteFileLocation::AsKey::store(StorerT &storer) const {
  using td::store;
  store(key.id_, storer);
  store(key.volume_id_, storer);
  store(key.local_id_, storer);
}

template <class StorerT>
void WebRemoteFileLocation::store(StorerT &storer) const {
  using td::store;
  store(url_, storer);
  store(access_hash_, storer);
}

template <class ParserT>
void WebRemoteFileLocation::parse(ParserT &parser) {
  using td::parse;
  parse(url_, parser);
  parse(access_hash_, parser);
}

template <class StorerT>
void WebRemoteFileLocation::AsKey::store(StorerT &storer) const {
  td::store(key.url_, storer);
}

template <class StorerT>
void CommonRemoteFileLocation::store(StorerT &storer) const {
  using td::store;
  store(id_, storer);
  store(access_hash_, storer);
}

template <class ParserT>
void CommonRemoteFileLocation::parse(ParserT &parser) {
  using td::parse;
  parse(id_, parser);
  parse(access_hash_, parser);
}

template <class StorerT>
void CommonRemoteFileLocation::AsKey::store(StorerT &storer) const {
  td::store(key.id_, storer);
}

template <class StorerT>
void FullRemoteFileLocation::store(StorerT &storer) const {
  using ::td::store;
  store(full_type(), storer);
  store(dc_id_.get_value(), storer);
  if (!file_reference_.empty()) {
    store(file_reference_, storer);
  }
  variant_.visit([&](auto &&value) {
    using td::store;
    store(value, storer);
  });
}

template <class ParserT>
void FullRemoteFileLocation::parse(ParserT &parser) {
  using ::td::parse;
  int32 raw_type;
  parse(raw_type, parser);
  web_location_flag_ = (raw_type & WEB_LOCATION_FLAG) != 0;
  raw_type &= ~WEB_LOCATION_FLAG;
  bool has_file_reference = (raw_type & FILE_REFERENCE_FLAG) != 0;
  raw_type &= ~FILE_REFERENCE_FLAG;
  if (raw_type < 0 || raw_type >= static_cast<int32>(FileType::Size)) {
    return parser.set_error("Invalid FileType in FullRemoteFileLocation");
  }
  file_type_ = static_cast<FileType>(raw_type);
  int32 dc_id_value;
  parse(dc_id_value, parser);
  dc_id_ = DcId::from_value(dc_id_value);

  if (has_file_reference) {
    parse(file_reference_, parser);
    file_reference_.clear();
  }

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

template <class StorerT>
void FullRemoteFileLocation::AsKey::store(StorerT &storer) const {
  using td::store;
  store(key.key_type(), storer);
  key.variant_.visit([&](auto &&value) {
    using td::store;
    store(value.as_key(), storer);
  });
}

template <class StorerT>
void RemoteFileLocation::store(StorerT &storer) const {
  storer.store_int(variant_.get_offset());
  bool ok{false};
  variant_.visit([&](auto &&value) {
    using td::store;
    store(value, storer);
    ok = true;
  });
  CHECK(ok);
}

template <class ParserT>
void RemoteFileLocation::parse(ParserT &parser) {
  auto type = static_cast<Type>(parser.fetch_int());
  switch (type) {
    case Type::Empty:
      variant_ = EmptyRemoteFileLocation();
      return;
    case Type::Partial:
      variant_ = PartialRemoteFileLocation();
      return partial().parse(parser);
    case Type::Full:
      variant_ = FullRemoteFileLocation();
      return full().parse(parser);
  }
  parser.set_error("Invalid type in RemoteFileLocation");
}

template <class StorerT>
void PartialLocalFileLocation::store(StorerT &storer) const {
  using td::store;
  store(file_type_, storer);
  store(path_, storer);
  store(part_size_, storer);
  int32 deprecated_ready_part_count = -1;
  store(deprecated_ready_part_count, storer);
  store(iv_, storer);
  store(ready_bitmask_, storer);
}

template <class ParserT>
void PartialLocalFileLocation::parse(ParserT &parser) {
  using td::parse;
  parse(file_type_, parser);
  if (file_type_ < FileType::Thumbnail || file_type_ >= FileType::Size) {
    return parser.set_error("Invalid type in PartialLocalFileLocation");
  }
  parse(path_, parser);
  parse(part_size_, parser);
  int32 deprecated_ready_part_count;
  parse(deprecated_ready_part_count, parser);
  parse(iv_, parser);
  if (deprecated_ready_part_count == -1) {
    parse(ready_bitmask_, parser);
  } else {
    CHECK(0 <= deprecated_ready_part_count);
    CHECK(deprecated_ready_part_count <= (1 << 22));
    ready_bitmask_ = Bitmask(Bitmask::Ones{}, deprecated_ready_part_count).encode();
  }
}

template <class StorerT>
void FullLocalFileLocation::store(StorerT &storer) const {
  using td::store;
  store(file_type_, storer);
  store(mtime_nsec_, storer);
  store(path_, storer);
}

template <class ParserT>
void FullLocalFileLocation::parse(ParserT &parser) {
  using td::parse;
  parse(file_type_, parser);
  if (file_type_ < FileType::Thumbnail || file_type_ >= FileType::Size) {
    return parser.set_error("Invalid type in FullLocalFileLocation");
  }
  parse(mtime_nsec_, parser);
  parse(path_, parser);
}

template <class StorerT>
void PartialLocalFileLocationPtr::store(StorerT &storer) const {
  td::store(*location_, storer);
}

template <class StorerT>
void LocalFileLocation::store(StorerT &storer) const {
  using td::store;
  store(variant_.get_offset(), storer);
  variant_.visit([&](auto &&value) {
    using td::store;
    store(value, storer);
  });
}

template <class ParserT>
void LocalFileLocation::parse(ParserT &parser) {
  using td::parse;
  auto type = static_cast<Type>(parser.fetch_int());
  switch (type) {
    case Type::Empty:
      variant_ = EmptyLocalFileLocation();
      return;
    case Type::Partial:
      variant_ = PartialLocalFileLocationPtr();
      return parse(partial(), parser);
    case Type::Full:
      variant_ = FullLocalFileLocation();
      return parse(full(), parser);
  }
  return parser.set_error("Invalid type in LocalFileLocation");
}

template <class StorerT>
void FullGenerateFileLocation::store(StorerT &storer) const {
  using td::store;
  store(file_type_, storer);
  store(original_path_, storer);
  store(conversion_, storer);
}

template <class ParserT>
void FullGenerateFileLocation::parse(ParserT &parser) {
  using td::parse;
  parse(file_type_, parser);
  parse(original_path_, parser);
  parse(conversion_, parser);
}

template <class StorerT>
void GenerateFileLocation::store(StorerT &storer) const {
  td::store(type_, storer);
  switch (type_) {
    case Type::Empty:
      return;
    case Type::Full:
      return td::store(full_, storer);
  }
}

template <class ParserT>
void GenerateFileLocation::parse(ParserT &parser) {
  td::parse(type_, parser);
  switch (type_) {
    case Type::Empty:
      return;
    case Type::Full:
      return td::parse(full_, parser);
  }
  return parser.set_error("Invalid type in GenerateFileLocation");
}

}  // namespace td
