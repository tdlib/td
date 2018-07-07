//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/FileLog.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/port/Fd.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/path.h"
#include "td/utils/Slice.h"

#include <limits>

namespace td {

bool FileLog::init(string path, int64 rotate_threshold) {
  if (path == path_) {
    set_rotate_threshold(rotate_threshold);
    return true;
  }

  auto r_fd = FileFd::open(path, FileFd::Create | FileFd::Write | FileFd::Append);
  if (r_fd.is_error()) {
    LOG(ERROR) << "Can't open log: " << r_fd.error();
    return false;
  }

  fd_.close();
  fd_ = r_fd.move_as_ok();
  Fd::duplicate(fd_.get_fd(), Fd::Stderr()).ignore();

  path_ = std::move(path);
  size_ = fd_.get_size();
  rotate_threshold_ = rotate_threshold;
  return true;
}

void FileLog::set_rotate_threshold(int64 rotate_threshold) {
  rotate_threshold_ = rotate_threshold;
}

void FileLog::append(CSlice cslice, int log_level) {
  Slice slice = cslice;
  while (!slice.empty()) {
    auto r_size = fd_.write(slice);
    if (r_size.is_error()) {
      process_fatal_error(r_size.error().message());
    }
    auto written = r_size.ok();
    size_ += static_cast<int64>(written);
    slice.remove_prefix(written);
  }
  if (log_level == VERBOSITY_NAME(FATAL)) {
    process_fatal_error(cslice);
  }

  if (size_ > rotate_threshold_) {
    auto status = rename(path_, PSLICE() << path_ << ".old");
    if (status.is_error()) {
      process_fatal_error(status.message());
    }
    do_rotate();
  }
}

void FileLog::rotate() {
  if (path_.empty()) {
    return;
  }
  do_rotate();
}

void FileLog::do_rotate() {
  auto current_verbosity_level = GET_VERBOSITY_LEVEL();
  SET_VERBOSITY_LEVEL(std::numeric_limits<int>::min());  // to ensure that nothing will be printed to the closed log
  CHECK(!path_.empty());
  fd_.close();
  auto r_fd = FileFd::open(path_, FileFd::Create | FileFd::Truncate | FileFd::Write);
  if (r_fd.is_error()) {
    process_fatal_error(r_fd.error().message());
  }
  fd_ = r_fd.move_as_ok();
  Fd::duplicate(fd_.get_fd(), Fd::Stderr()).ignore();
  size_ = 0;
  SET_VERBOSITY_LEVEL(current_verbosity_level);
}

}  // namespace td
