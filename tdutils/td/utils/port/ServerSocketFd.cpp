//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/ServerSocketFd.h"

#include "td/utils/port/config.h"

#include "td/utils/logging.h"
#include "td/utils/port/IPAddress.h"

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

Result<ServerSocketFd> ServerSocketFd::open(int32 port, CSlice addr) {
  ServerSocketFd socket;
  TRY_STATUS(socket.init(port, addr));
  return std::move(socket);
}

const Fd &ServerSocketFd::get_fd() const {
  return fd_;
}

Fd &ServerSocketFd::get_fd() {
  return fd_;
}

int32 ServerSocketFd::get_flags() const {
  return fd_.get_flags();
}

Status ServerSocketFd::get_pending_error() {
  return fd_.get_pending_error();
}

Result<SocketFd> ServerSocketFd::accept() {
#if TD_PORT_POSIX
  sockaddr_storage addr;
  socklen_t addr_len = sizeof(addr);
  int native_fd = fd_.get_native_fd();
  int r_fd = skip_eintr([&] { return ::accept(native_fd, reinterpret_cast<sockaddr *>(&addr), &addr_len); });
  auto accept_errno = errno;
  if (r_fd >= 0) {
    return SocketFd::from_native_fd(r_fd);
  }

  if (accept_errno == EAGAIN
#if EAGAIN != EWOULDBLOCK
      || accept_errno == EWOULDBLOCK
#endif
  ) {
    fd_.clear_flags(Fd::Read);
    return Status::Error(-1, "Operation would block");
  }

  auto error = Status::PosixError(accept_errno, PSLICE() << "Accept from [fd = " << native_fd << "] has failed");
  switch (accept_errno) {
    case EBADF:
    case EFAULT:
    case EINVAL:
    case ENOTSOCK:
    case EOPNOTSUPP:
      LOG(FATAL) << error;
      UNREACHABLE();
      break;
    default:
      LOG(ERROR) << error;
    // fallthrough
    case EMFILE:
    case ENFILE:
    case ECONNABORTED:  //???
      fd_.clear_flags(Fd::Read);
      fd_.update_flags(Fd::Close);
      return std::move(error);
  }
#elif TD_PORT_WINDOWS
  TRY_RESULT(socket_fd, fd_.accept());
  return SocketFd(std::move(socket_fd));
#endif
}

void ServerSocketFd::close() {
  fd_.close();
}

bool ServerSocketFd::empty() const {
  return fd_.empty();
}

Status ServerSocketFd::init(int32 port, CSlice addr) {
  IPAddress address;
  TRY_STATUS(address.init_ipv4_port(addr, port));
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

  linger ling = {0, 0};
#if TD_PORT_POSIX
  int flags = 1;
#ifdef SO_REUSEPORT
  setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char *>(&flags), sizeof(flags));
#endif
#elif TD_PORT_WINDOWS
  BOOL flags = TRUE;
#endif
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&flags), sizeof(flags));
  setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char *>(&flags), sizeof(flags));
  setsockopt(fd, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char *>(&ling), sizeof(ling));
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&flags), sizeof(flags));

  int e_bind = bind(fd, address.get_sockaddr(), static_cast<socklen_t>(address.get_sockaddr_len()));
  if (e_bind != 0) {
    return OS_SOCKET_ERROR("Failed to bind a socket");
  }

  // TODO: magic constant
  int e_listen = listen(fd, 8192);
  if (e_listen != 0) {
    return OS_SOCKET_ERROR("Failed to listen on a socket");
  }

#if TD_PORT_POSIX
  fd_ = Fd(fd, Fd::Mode::Owner);
#elif TD_PORT_WINDOWS
  fd_ = Fd::create_server_socket_fd(fd, address.get_address_family());
#endif

  fd_quard.dismiss();
  return Status::OK();
}

}  // namespace td
