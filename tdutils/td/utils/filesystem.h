//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/buffer.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

namespace td {

Result<BufferSlice> read_file(CSlice path, int64 size = -1);

Status copy_file(CSlice from, CSlice to, int64 size = -1);

Status write_file(CSlice to, Slice data);

std::string clean_filename(CSlice name);
}  // namespace td
