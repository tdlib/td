//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <utility>

namespace td {
enum class FileType : int8;

Result<std::pair<FileFd, string>> open_temp_file(const FileType &file_type) TD_WARN_UNUSED_RESULT;
Result<string> create_from_temp(CSlice temp_path, CSlice dir, CSlice name) TD_WARN_UNUSED_RESULT;
string get_files_base_dir(const FileType &file_type);
string get_files_temp_dir(const FileType &file_type);
string get_files_dir(const FileType &file_type);
}  // namespace td
