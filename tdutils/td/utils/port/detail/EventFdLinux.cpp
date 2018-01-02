//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/detail/EventFdLinux.h"

char disable_linker_warning_about_empty_file_event_fd_linux_cpp TD_UNUSED;

#ifdef TD_EVENTFD_LINUX

#include "td/utils/logging.h"
#include "td/utils/Slice.h"

#include <sys/eventfd.h>

namespace td {
namespace detail {

void EventFdLinux::init() {
  int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  auto eventfd_errno = errno;
  LOG_IF(FATAL, fd == -1) << Status::PosixError(eventfd_errno, "eventfd call failed");

  fd_ = Fd(fd, Fd::Mode::Owner);
}

bool EventFdLinux::empty() {
  return fd_.empty();
}

void EventFdLinux::close() {
  fd_.close();
}

Status EventFdLinux::get_pending_error() {
  return Status::OK();
}

const Fd &EventFdLinux::get_fd() const {
  return fd_;
}

Fd &EventFdLinux::get_fd() {
  return fd_;
}

void EventFdLinux::release() {
  const uint64 value = 1;
  // NB: write_unsafe is used, because release will be called from multiple threads
  auto result = fd_.write_unsafe(Slice(reinterpret_cast<const char *>(&value), sizeof(value)));
  if (result.is_error()) {
    LOG(FATAL) << "EventFdLinux write failed: " << result.error();
  }
  size_t size = result.ok();
  if (size != sizeof(value)) {
    LOG(FATAL) << "EventFdLinux write returned " << value << " instead of " << sizeof(value);
  }
}

void EventFdLinux::acquire() {
  uint64 res;
  auto result = fd_.read(MutableSlice(reinterpret_cast<char *>(&res), sizeof(res)));
  if (result.is_error()) {
    LOG(FATAL) << "EventFdLinux read failed: " << result.error();
  }
  fd_.clear_flags(Fd::Read);
}

}  // namespace detail
}  // namespace td

#endif
