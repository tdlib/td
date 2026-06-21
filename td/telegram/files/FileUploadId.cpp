//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/files/FileUploadId.h"

#include "td/telegram/files/FileManager.h"

#include "td/utils/algorithm.h"

namespace td {

vector<FileUploadId> FileUploadId::get_file_upload_ids(const vector<FileId> &file_ids) {
  return transform(file_ids, [](FileId file_id) {
    return file_id.is_valid() ? FileUploadId(file_id, FileManager::get_internal_upload_id()) : FileUploadId();
  });
}

FileUploadId FileUploadId::get_file_upload_id(const vector<FileUploadId> *file_upload_ids, int32 media_pos) {
  if (file_upload_ids == nullptr || file_upload_ids->empty()) {
    return FileUploadId();
  }
  if (media_pos == -1) {
    CHECK(file_upload_ids->size() == 1u);
    return (*file_upload_ids)[0];
  }
  CHECK(static_cast<size_t>(media_pos) < (*file_upload_ids).size());
  return (*file_upload_ids)[media_pos];
}

}  // namespace td
