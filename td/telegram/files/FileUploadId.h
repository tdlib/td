//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/HashTableUtils.h"
#include "td/utils/StringBuilder.h"

#include <type_traits>

namespace td {

class FileUploadId {
  const FileId file_id_;
  const int64 internal_upload_id_;

 public:
  FileUploadId(FileId file_id, int64 internal_upload_id) : file_id_(file_id), internal_upload_id_(internal_upload_id) {
  }

  FileId get_file_id() const {
    return file_id_;
  }

  int64 get_internal_upload_id() const {
    return internal_upload_id_;
  }

  bool operator==(const FileUploadId &other) const {
    return file_id_ == other.file_id_ && internal_upload_id_ == other.internal_upload_id_;
  }

  bool operator!=(const FileUploadId &other) const {
    return !(*this == other);
  }
};

struct FileUploadIdHash {
  uint32 operator()(FileUploadId file_id) const {
    return combine_hashes(FileIdHash()(file_id.get_file_id()), Hash<int64>()(file_id.get_internal_upload_id()));
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, FileUploadId file_upload_id) {
  return string_builder << file_upload_id.get_file_id() << '+' << file_upload_id.get_internal_upload_id();
}

}  // namespace td
