//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/detail/Select.h"

char disable_linker_warning_about_empty_file_select_cpp TD_UNUSED;

#ifdef TD_POLL_SELECT

#include "td/utils/logging.h"

#include <utility>

namespace td {
namespace detail {

void Select::init() {
  FD_ZERO(&all_fd_);
  FD_ZERO(&read_fd_);
  FD_ZERO(&write_fd_);
  FD_ZERO(&except_fd_);
  max_fd_ = -1;
}

void Select::clear() {
  fds_.clear();
}

void Select::subscribe(const Fd &fd, Fd::Flags flags) {
  int native_fd = fd.get_native_fd();
  for (auto &it : fds_) {
    CHECK(it.fd_ref.get_native_fd() != native_fd);
  }
  fds_.push_back(FdInfo{Fd(native_fd, Fd::Mode::Reference), flags});
  CHECK(0 <= native_fd && native_fd < FD_SETSIZE) << native_fd << " " << FD_SETSIZE;
  FD_SET(native_fd, &all_fd_);
  if (native_fd > max_fd_) {
    max_fd_ = native_fd;
  }
}

void Select::unsubscribe(const Fd &fd) {
  int native_fd = fd.get_native_fd();
  CHECK(0 <= native_fd && native_fd < FD_SETSIZE) << native_fd << " " << FD_SETSIZE;
  FD_CLR(native_fd, &all_fd_);
  FD_CLR(native_fd, &read_fd_);
  FD_CLR(native_fd, &write_fd_);
  FD_CLR(native_fd, &except_fd_);
  while (max_fd_ >= 0 && !FD_ISSET(max_fd_, &all_fd_)) {
    max_fd_--;
  }
  for (auto it = fds_.begin(); it != fds_.end();) {
    if (it->fd_ref.get_native_fd() == native_fd) {
      std::swap(*it, fds_.back());
      fds_.pop_back();
      break;
    } else {
      ++it;
    }
  }
}

void Select::unsubscribe_before_close(const Fd &fd) {
  unsubscribe(fd);
}

void Select::run(int timeout_ms) {
  timeval timeout_data;
  timeval *timeout_ptr;
  if (timeout_ms == -1) {
    timeout_ptr = nullptr;
  } else {
    timeout_data.tv_sec = timeout_ms / 1000;
    timeout_data.tv_usec = timeout_ms % 1000 * 1000;
    timeout_ptr = &timeout_data;
  }

  for (auto &it : fds_) {
    int native_fd = it.fd_ref.get_native_fd();
    Fd::Flags fd_flags = it.fd_ref.get_flags();
    if ((it.flags & Fd::Write) && !(fd_flags & Fd::Write)) {
      FD_SET(native_fd, &write_fd_);
    } else {
      FD_CLR(native_fd, &write_fd_);
    }
    if ((it.flags & Fd::Read) && !(fd_flags & Fd::Read)) {
      FD_SET(native_fd, &read_fd_);
    } else {
      FD_CLR(native_fd, &read_fd_);
    }
    FD_SET(native_fd, &except_fd_);
  }

  select(max_fd_ + 1, &read_fd_, &write_fd_, &except_fd_, timeout_ptr);
  for (auto &it : fds_) {
    int native_fd = it.fd_ref.get_native_fd();
    Fd::Flags flags = 0;
    if (FD_ISSET(native_fd, &read_fd_)) {
      flags |= Fd::Read;
    }
    if (FD_ISSET(native_fd, &write_fd_)) {
      flags |= Fd::Write;
    }
    if (FD_ISSET(native_fd, &except_fd_)) {
      flags |= Fd::Error;
    }
    if (flags != 0) {
      it.fd_ref.update_flags_notify(flags);
    }
  }
}

}  // namespace detail
}  // namespace td

#endif
