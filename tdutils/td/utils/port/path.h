//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <functional>
#include <utility>

namespace td {

Status mkdir(CSlice dir, int32 mode = 0700) TD_WARN_UNUSED_RESULT;

Status mkpath(CSlice path, int32 mode = 0700) TD_WARN_UNUSED_RESULT;

Status rename(CSlice from, CSlice to) TD_WARN_UNUSED_RESULT;

Result<string> realpath(CSlice slice, bool ignore_access_denied = false) TD_WARN_UNUSED_RESULT;

Status chdir(CSlice dir) TD_WARN_UNUSED_RESULT;

Status rmdir(CSlice dir) TD_WARN_UNUSED_RESULT;

Status unlink(CSlice path) TD_WARN_UNUSED_RESULT;

Status rmrf(CSlice path) TD_WARN_UNUSED_RESULT;

Status set_temporary_dir(CSlice dir) TD_WARN_UNUSED_RESULT;

CSlice get_temporary_dir();

Result<std::pair<FileFd, string>> mkstemp(CSlice dir) TD_WARN_UNUSED_RESULT;

Result<string> mkdtemp(CSlice dir, Slice prefix) TD_WARN_UNUSED_RESULT;

Status walk_path(CSlice path, const std::function<void(CSlice name, bool is_directory)> &func) TD_WARN_UNUSED_RESULT;

}  // namespace td
