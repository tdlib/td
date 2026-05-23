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

}  // namespace td
