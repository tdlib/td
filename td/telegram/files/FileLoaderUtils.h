//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/files/FileLocation.h"
#include "td/telegram/files/FileType.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <utility>

namespace td {

Result<std::pair<FileFd, string>> open_temp_file(FileType file_type) TD_WARN_UNUSED_RESULT;

Result<string> create_from_temp(CSlice temp_path, CSlice dir, CSlice name) TD_WARN_UNUSED_RESULT;

Result<string> search_file(CSlice dir, CSlice name, int64 expected_size) TD_WARN_UNUSED_RESULT;

Result<FullLocalFileLocation> save_file_bytes(FileType type, BufferSlice bytes, CSlice file_name);

Slice get_files_base_dir(FileType file_type);

string get_files_temp_dir(FileType file_type);

string get_files_dir(FileType file_type);

}  // namespace td
