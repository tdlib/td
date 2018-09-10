//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#if 0

namespace td {

#if TD_PORT_POSIX

Status Fd::duplicate(const Fd &from, Fd &to) {
  CHECK(!from.empty());
  CHECK(!to.empty());
  if (dup2(from.get_native_fd(), to.get_native_fd()) == -1) {
    return OS_ERROR("Failed to duplicate file descriptor");
  }
  return Status::OK();
}

Status Fd::set_is_blocking(bool is_blocking) {
  auto old_flags = fcntl(fd_, F_GETFL);
  if (old_flags == -1) {
    return OS_SOCKET_ERROR("Failed to get socket flags");
  }
  auto new_flags = is_blocking ? old_flags & ~O_NONBLOCK : old_flags | O_NONBLOCK;
  if (new_flags != old_flags && fcntl(fd_, F_SETFL, new_flags) == -1) {
    return OS_SOCKET_ERROR("Failed to set socket flags");
  }

  return Status::OK();
}

#endif

#if TD_PORT_WINDOWS

#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP | WINAPI_PARTITION_SYSTEM)
Fd &Fd::Stderr() {
  static auto handle = GetStdHandle(STD_ERROR_HANDLE);
  LOG_IF(FATAL, handle == INVALID_HANDLE_VALUE) << "Failed to get stderr";
  static auto fd = Fd(Fd::Type::FileFd, Fd::Mode::Reference, handle);
  return fd;
}
Fd &Fd::Stdin() {
  static auto handle = GetStdHandle(STD_INPUT_HANDLE);
  LOG_IF(FATAL, handle == INVALID_HANDLE_VALUE) << "Failed to get stdin";
  static auto fd = Fd(Fd::Type::FileFd, Fd::Mode::Reference, handle);
  return fd;
}
Fd &Fd::Stdout() {
  static auto handle = GetStdHandle(STD_OUTPUT_HANDLE);
  LOG_IF(FATAL, handle == INVALID_HANDLE_VALUE) << "Failed to get stdout";
  static auto fd = Fd(Fd::Type::FileFd, Fd::Mode::Reference, handle);
  return fd;
}
#else
Fd &Fd::Stderr() {
  static Fd result;
  result = Fd();
  return result;
}
Fd &Fd::Stdin() {
  static Fd result;
  result = Fd();
  return result;
}
Fd &Fd::Stdout() {
  static Fd result;
  result = Fd();
  return result;
}
#endif

Status Fd::duplicate(const Fd &from, Fd &to) {
  return Status::Error("Not supported");
}

#endif

}  // namespace td
#endif
