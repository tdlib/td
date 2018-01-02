//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/detail/EventFdBsd.h"

char disable_linker_warning_about_empty_file_event_fd_bsd_cpp TD_UNUSED;

#ifdef TD_EVENTFD_BSD

#include "td/utils/logging.h"
#include "td/utils/Slice.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>

namespace td {
namespace detail {

// TODO: it is extremely non optimal on Darwin. kqueue events should be used instead
void EventFdBsd::init() {
  int fds[2];
  int err = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
  auto socketpair_errno = errno;
#if TD_CYGWIN
  // it looks like CYGWIN bug
  int max_retries = 1000000;
  while (err == -1 && socketpair_errno == EADDRINUSE && max_retries-- > 0) {
    err = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    socketpair_errno = errno;
  }
// LOG_IF(ERROR, max_retries < 1000000) << max_retries;
#endif
  LOG_IF(FATAL, err == -1) << Status::PosixError(socketpair_errno, "socketpair failed");

  detail::set_native_socket_is_blocking(fds[0], false).ensure();
  detail::set_native_socket_is_blocking(fds[1], false).ensure();

  in_ = Fd(fds[0], Fd::Mode::Owner);
  out_ = Fd(fds[1], Fd::Mode::Owner);
}

bool EventFdBsd::empty() {
  return in_.empty();
}

void EventFdBsd::close() {
  in_.close();
  out_.close();
}

Status EventFdBsd::get_pending_error() {
  return Status::OK();
}

const Fd &EventFdBsd::get_fd() const {
  return out_;
}

Fd &EventFdBsd::get_fd() {
  return out_;
}

void EventFdBsd::release() {
  int value = 1;
  auto result = in_.write(Slice(reinterpret_cast<const char *>(&value), sizeof(value)));
  if (result.is_error()) {
    LOG(FATAL) << "EventFdBsd write failed: " << result.error();
  }
  size_t size = result.ok();
  if (size != sizeof(value)) {
    LOG(FATAL) << "EventFdBsd write returned " << value << " instead of " << sizeof(value);
  }
}

void EventFdBsd::acquire() {
  out_.update_flags(Fd::Read);
  while (can_read(out_)) {
    uint8 value[1024];
    auto result = out_.read(MutableSlice(value, sizeof(value)));
    if (result.is_error()) {
      LOG(FATAL) << "EventFdBsd read failed:" << result.error();
    }
  }
}

}  // namespace detail
}  // namespace td

#endif
