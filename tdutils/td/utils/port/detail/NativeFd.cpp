//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/detail/NativeFd.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"

#if TD_PORT_POSIX
#include <unistd.h>
#endif

namespace td {

NativeFd::NativeFd(Raw raw) : fd_(raw) {
  VLOG(fd) << *this << " create";
}

NativeFd::NativeFd(Raw raw, bool nolog) : fd_(raw) {
}

#if TD_PORT_WINDOWS
NativeFd::NativeFd(SOCKET raw) : fd_(reinterpret_cast<HANDLE>(raw)), is_socket_(true) {
  VLOG(fd) << *this << " create";
}
#endif

NativeFd::~NativeFd() {
  close();
}

NativeFd::operator bool() const {
  return fd_.get() != empty_raw();
}

NativeFd::Raw NativeFd::empty_raw() {
#if TD_PORT_POSIX
  return -1;
#elif TD_PORT_WINDOWS
  return INVALID_HANDLE_VALUE;
#endif
}

NativeFd::Raw NativeFd::raw() const {
  return fd_.get();
}

NativeFd::Raw NativeFd::fd() const {
  return raw();
}

#if TD_PORT_WINDOWS
NativeFd::Raw NativeFd::io_handle() const {
  return raw();
}
SOCKET NativeFd::socket() const {
  CHECK(is_socket_);
  return reinterpret_cast<SOCKET>(fd_.get());
}
#elif TD_PORT_POSIX
NativeFd::Raw NativeFd::socket() const {
  return raw();
}
#endif

Status NativeFd::set_is_blocking(bool is_blocking) const {
#if TD_PORT_POSIX
  auto old_flags = fcntl(fd(), F_GETFL);
  if (old_flags == -1) {
    return OS_SOCKET_ERROR("Failed to get socket flags");
  }
  auto new_flags = is_blocking ? old_flags & ~O_NONBLOCK : old_flags | O_NONBLOCK;
  if (new_flags != old_flags && fcntl(fd(), F_SETFL, new_flags) == -1) {
    return OS_SOCKET_ERROR("Failed to set socket flags");
  }

  return Status::OK();
#elif TD_PORT_WINDOWS
  return set_is_blocking_unsafe(is_blocking);
#endif
}

Status NativeFd::set_is_blocking_unsafe(bool is_blocking) const {
#if TD_PORT_POSIX
  if (fcntl(fd(), F_SETFL, is_blocking ? 0 : O_NONBLOCK) == -1) {
#elif TD_PORT_WINDOWS
  u_long mode = is_blocking;
  if (ioctlsocket(socket(), FIONBIO, &mode) != 0) {
#endif
    return OS_SOCKET_ERROR("Failed to change socket flags");
  }
  return Status::OK();
}

Status NativeFd::duplicate(const NativeFd &to) const {
#if TD_PORT_POSIX
  CHECK(*this);
  CHECK(to);
  if (dup2(fd(), to.fd()) == -1) {
    return OS_ERROR("Failed to duplicate file descriptor");
  }
  return Status::OK();
#elif TD_PORT_WINDOWS
  return Status::Error("Not supported");
#endif
}

void NativeFd::close() {
  if (!*this) {
    return;
  }
  VLOG(fd) << *this << " close";
#if TD_PORT_WINDOWS
  if (is_socket_ ? closesocket(socket()) : !CloseHandle(io_handle())) {
#elif TD_PORT_POSIX
  if (::close(fd()) < 0) {
#endif
    auto error = OS_ERROR("Close fd");
    LOG(ERROR) << error;
  }
  fd_ = {};
}

NativeFd::Raw NativeFd::release() {
  VLOG(fd) << *this << " release";
  auto res = fd_.get();
  fd_ = {};
  return res;
}

StringBuilder &operator<<(StringBuilder &sb, const NativeFd &fd) {
  return sb << tag("fd", fd.raw());
}

}  // namespace td
