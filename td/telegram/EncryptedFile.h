//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tl_helpers.h"

namespace td {

struct EncryptedFile {
  static constexpr int32 MAGIC = 0x473d738a;
  int64 id_ = 0;
  int64 access_hash_ = 0;
  int32 size_ = 0;
  int32 dc_id_ = 0;
  int32 key_fingerprint_ = 0;

  EncryptedFile() = default;
  EncryptedFile(int64 id, int64 access_hash, int32 size, int32 dc_id, int32 key_fingerprint)
      : id_(id), access_hash_(access_hash), size_(size), dc_id_(dc_id), key_fingerprint_(key_fingerprint) {
  }

  static unique_ptr<EncryptedFile> get_encrypted_file(tl_object_ptr<telegram_api::EncryptedFile> file_ptr) {
    if (file_ptr == nullptr || file_ptr->get_id() != telegram_api::encryptedFile::ID) {
      return nullptr;
    }
    auto file = move_tl_object_as<telegram_api::encryptedFile>(file_ptr);
    return make_unique<EncryptedFile>(file->id_, file->access_hash_, file->size_, file->dc_id_, file->key_fingerprint_);
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using td::store;
    store(MAGIC, storer);
    store(id_, storer);
    store(access_hash_, storer);
    store(size_, storer);
    store(dc_id_, storer);
    store(key_fingerprint_, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using td::parse;
    int32 got_magic;

    parse(got_magic, parser);
    parse(id_, parser);
    parse(access_hash_, parser);
    parse(size_, parser);
    parse(dc_id_, parser);
    parse(key_fingerprint_, parser);

    if (got_magic != MAGIC) {
      parser.set_error("EncryptedFile magic mismatch");
    }
  }
};

inline StringBuilder &operator<<(StringBuilder &sb, const EncryptedFile &file) {
  return sb << "[" << tag("id", file.id_) << tag("access_hash", file.access_hash_) << tag("size", file.size_)
            << tag("dc_id", file.dc_id_) << tag("key_fingerprint", file.key_fingerprint_) << "]";
}

}  // namespace td