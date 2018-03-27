//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/SecureValue.h"

#include "td/telegram/files/FileId.hpp"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void store(SecureFile file, StorerT &storer) {
  store(file.file_id, storer);
  store(file.file_hash, storer);
  store(file.encrypted_secret, storer);
}

template <class ParserT>
void parse(SecureFile &file, ParserT &parser) {
  parse(file.file_id, parser);
  parse(file.file_hash, parser);
  parse(file.encrypted_secret, parser);
}

template <class StorerT>
void store(const SecureData &data, StorerT &storer) {
  store(data.data, storer);
  store(data.hash, storer);
  store(data.encrypted_secret, storer);
}

template <class ParserT>
void parse(SecureData &data, ParserT &parser) {
  parse(data.data, parser);
  parse(data.hash, parser);
  parse(data.encrypted_secret, parser);
}

template <class StorerT>
void store(const SecureCredentials &credentials, StorerT &storer) {
  store(credentials.data, storer);
  store(credentials.hash, storer);
  store(credentials.encrypted_secret, storer);
}

template <class ParserT>
void parse(SecureCredentials &credentials, ParserT &parser) {
  parse(credentials.data, parser);
  parse(credentials.hash, parser);
  parse(credentials.encrypted_secret, parser);
}

template <class StorerT>
void store(const EncryptedSecureValue &value, StorerT &storer) {
  bool has_data_hash = !value.data.hash.empty();
  bool has_files = !value.files.empty();
  bool has_selfie = value.selfie.file_id.is_valid();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_data_hash);
  STORE_FLAG(has_files);
  STORE_FLAG(has_selfie);
  END_STORE_FLAGS();
  store(value.type, storer);
  if (has_data_hash) {
    store(value.data, storer);
  } else {
    store(value.data.data, storer);
  }
  if (has_files) {
    store(value.files, storer);
  }
  if (has_selfie) {
    store(value.selfie, storer);
  }
}

template <class ParserT>
void parse(EncryptedSecureValue &value, ParserT &parser) {
  bool has_data_hash;
  bool has_files;
  bool has_selfie;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_data_hash);
  PARSE_FLAG(has_files);
  PARSE_FLAG(has_selfie);
  END_PARSE_FLAGS();
  parse(value.type, parser);
  if (has_data_hash) {
    parse(value.data, parser);
  } else {
    parse(value.data.data, parser);
  }
  if (has_files) {
    parse(value.files, parser);
  }
  if (has_selfie) {
    parse(value.selfie, parser);
  }
}

}  // namespace td
