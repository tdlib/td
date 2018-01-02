//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/port/config.h"

#include "td/utils/common.h"
#include "td/utils/port/Fd.h"
#include "td/utils/port/Stat.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

namespace td {

class FileFd {
 public:
  FileFd() = default;

  enum Flags : int32 { Write = 1, Read = 2, Truncate = 4, Create = 8, Append = 16, CreateNew = 32 };

  const Fd &get_fd() const;
  Fd &get_fd();

  static Result<FileFd> open(CSlice filepath, int32 flags, int32 mode = 0600) TD_WARN_UNUSED_RESULT;

  Result<size_t> write(Slice slice) TD_WARN_UNUSED_RESULT;
  Result<size_t> read(MutableSlice slice) TD_WARN_UNUSED_RESULT;

  Result<size_t> pwrite(Slice slice, int64 offset) TD_WARN_UNUSED_RESULT;
  Result<size_t> pread(MutableSlice slice, int64 offset) TD_WARN_UNUSED_RESULT;

  enum class LockFlags { Write, Read, Unlock };
  Status lock(LockFlags flags, int32 max_tries = 1) TD_WARN_UNUSED_RESULT;

  void close();
  bool empty() const;

  int32 get_flags() const;
  void update_flags(Fd::Flags mask);

  int64 get_size();

  Stat stat();

  Status sync() TD_WARN_UNUSED_RESULT;

  Status seek(int64 position) TD_WARN_UNUSED_RESULT;

  Status truncate_to_current_position(int64 current_position) TD_WARN_UNUSED_RESULT;

#if TD_PORT_POSIX
  int get_native_fd() const;
#endif

 private:
  Fd fd_;
};

}  // namespace td
