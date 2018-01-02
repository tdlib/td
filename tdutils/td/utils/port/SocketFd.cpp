//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/SocketFd.h"

#include "td/utils/logging.h"

#if TD_PORT_WINDOWS
#include "td/utils/misc.h"
#endif

#if TD_PORT_POSIX
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace td {

Result<SocketFd> SocketFd::open(const IPAddress &address) {
  SocketFd socket;
  TRY_STATUS(socket.init(address));
  return std::move(socket);
}

#if TD_PORT_POSIX
Result<SocketFd> SocketFd::from_native_fd(int fd) {
  auto fd_guard = ScopeExit() + [fd]() { ::close(fd); };

  TRY_STATUS(detail::set_native_socket_is_blocking(fd, false));

  // TODO remove copypaste
  int flags = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&flags), sizeof(flags));
  setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char *>(&flags), sizeof(flags));
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&flags), sizeof(flags));
  // TODO: SO_REUSEADDR, SO_KEEPALIVE, TCP_NODELAY, SO_SNDBUF, SO_RCVBUF, TCP_QUICKACK, SO_LINGER

  fd_guard.dismiss();

  SocketFd socket;
  socket.fd_ = Fd(fd, Fd::Mode::Owner);
  return std::move(socket);
}
#endif

Status SocketFd::init(const IPAddress &address) {
  auto fd = socket(address.get_address_family(), SOCK_STREAM, 0);
#if TD_PORT_POSIX
  if (fd == -1) {
#elif TD_PORT_WINDOWS
  if (fd == INVALID_SOCKET) {
#endif
    return OS_SOCKET_ERROR("Failed to create a socket");
  }
  auto fd_quard = ScopeExit() + [fd]() {
#if TD_PORT_POSIX
    ::close(fd);
#elif TD_PORT_WINDOWS
    ::closesocket(fd);
#endif
  };

  TRY_STATUS(detail::set_native_socket_is_blocking(fd, false));

#if TD_PORT_POSIX
  int flags = 1;
#elif TD_PORT_WINDOWS
  BOOL flags = TRUE;
#endif
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&flags), sizeof(flags));
  setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char *>(&flags), sizeof(flags));
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&flags), sizeof(flags));
  // TODO: SO_REUSEADDR, SO_KEEPALIVE, TCP_NODELAY, SO_SNDBUF, SO_RCVBUF, TCP_QUICKACK, SO_LINGER

#if TD_PORT_POSIX
  int e_connect = connect(fd, address.get_sockaddr(), static_cast<socklen_t>(address.get_sockaddr_len()));
  if (e_connect == -1) {
    auto connect_errno = errno;
    if (connect_errno != EINPROGRESS) {
      return Status::PosixError(connect_errno, PSLICE() << "Failed to connect to " << address);
    }
  }
  fd_ = Fd(fd, Fd::Mode::Owner);
#elif TD_PORT_WINDOWS
  auto bind_addr = address.get_any_addr();
  auto e_bind = bind(fd, bind_addr.get_sockaddr(), narrow_cast<int>(bind_addr.get_sockaddr_len()));
  if (e_bind != 0) {
    return OS_SOCKET_ERROR("Failed to bind a socket");
  }

  fd_ = Fd::create_socket_fd(fd);
  fd_.connect(address);
#endif

  fd_quard.dismiss();
  return Status::OK();
}

const Fd &SocketFd::get_fd() const {
  return fd_;
}

Fd &SocketFd::get_fd() {
  return fd_;
}

void SocketFd::close() {
  fd_.close();
}

bool SocketFd::empty() const {
  return fd_.empty();
}

int32 SocketFd::get_flags() const {
  return fd_.get_flags();
}

Status SocketFd::get_pending_error() {
  return fd_.get_pending_error();
}

Result<size_t> SocketFd::write(Slice slice) {
  return fd_.write(slice);
}

Result<size_t> SocketFd::read(MutableSlice slice) {
  return fd_.read(slice);
}

}  // namespace td
