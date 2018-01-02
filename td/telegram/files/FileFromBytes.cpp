//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/files/FileFromBytes.h"

#include "td/telegram/files/FileLoaderUtils.h"
#include "td/telegram/Global.h"

#include "td/utils/common.h"
#include "td/utils/misc.h"

#include <tuple>

namespace td {

FileFromBytes::FileFromBytes(FileType type, BufferSlice bytes, string name, std::unique_ptr<Callback> callback)
    : type_(type), bytes_(std::move(bytes)), name_(std::move(name)), callback_(std::move(callback)) {
}

void FileFromBytes::wakeup() {
  auto r_fd_path = open_temp_file(type_);
  if (r_fd_path.is_error()) {
    return callback_->on_error(r_fd_path.move_as_error());
  }
  FileFd fd;
  string path;
  std::tie(fd, path) = r_fd_path.move_as_ok();

  auto r_size = fd.write(bytes_.as_slice());
  if (r_size.is_error()) {
    return callback_->on_error(r_size.move_as_error());
  }
  fd.close();
  auto size = r_size.ok();
  if (size != bytes_.size()) {
    return callback_->on_error(Status::Error("Failed to write bytes to the file"));
  }

  auto dir = get_files_dir(type_);
  auto r_perm_path = create_from_temp(path, dir, name_);
  if (r_perm_path.is_error()) {
    return callback_->on_error(r_perm_path.move_as_error());
  }
  callback_->on_ok(FullLocalFileLocation(type_, r_perm_path.move_as_ok(), 0), narrow_cast<int64>(bytes_.size()));
}

}  // namespace td
