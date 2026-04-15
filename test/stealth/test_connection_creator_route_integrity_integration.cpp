// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/ConnectionCreator.h"

#include "td/utils/port/config.h"
#include "td/utils/tests.h"

#if TD_PORT_POSIX

#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

td::IPAddress ipv4_address(td::CSlice ip, td::int32 port) {
  td::IPAddress result;
  result.init_ipv4_port(ip, port).ensure();
  return result;
}

class Ipv4LoopbackListener final {
 public:
  static td::Result<Ipv4LoopbackListener> create() {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
      return td::Status::PosixError(errno, "Failed to create IPv4 listener socket");
    }

    int flags = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&flags), sizeof(flags));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (::bind(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) != 0) {
      auto error = td::Status::PosixError(errno, "Failed to bind IPv4 listener socket");
      ::close(fd);
      return error;
    }
    if (::listen(fd, 1) != 0) {
      auto error = td::Status::PosixError(errno, "Failed to listen on IPv4 listener socket");
      ::close(fd);
      return error;
    }

    socklen_t len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &len) != 0) {
      auto error = td::Status::PosixError(errno, "Failed to read IPv4 listener socket name");
      ::close(fd);
      return error;
    }

    return Ipv4LoopbackListener(fd, ntohs(addr.sin_port));
  }

  Ipv4LoopbackListener(Ipv4LoopbackListener &&other) noexcept : fd_(other.fd_), port_(other.port_) {
    other.fd_ = -1;
    other.port_ = 0;
  }

  Ipv4LoopbackListener &operator=(Ipv4LoopbackListener &&other) noexcept {
    if (this != &other) {
      reset();
      fd_ = other.fd_;
      port_ = other.port_;
      other.fd_ = -1;
      other.port_ = 0;
    }
    return *this;
  }

  ~Ipv4LoopbackListener() {
    reset();
  }

  td::int32 port() const {
    return port_;
  }

 private:
  Ipv4LoopbackListener(int fd, td::int32 port) : fd_(fd), port_(port) {
  }

  void reset() {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  int fd_{-1};
  td::int32 port_{0};
};

TEST(ConnectionCreatorRouteIntegrityIntegration, DirectSocketReportsExpectedLoopbackEndpoint) {
  auto listener = Ipv4LoopbackListener::create();
  ASSERT_TRUE(listener.is_ok());

  auto socket_result = td::SocketFd::open(ipv4_address("127.0.0.1", listener.ok().port()));
  ASSERT_TRUE(socket_result.is_ok());

  auto socket_fd = socket_result.move_as_ok();
  td::IPAddress actual_peer_address;
  ASSERT_TRUE(actual_peer_address.init_peer_address(socket_fd).is_ok());

  ASSERT_TRUE(td::ConnectionCreator::verify_connection_peer(
                  td::Proxy(), ipv4_address("127.0.0.1", listener.ok().port()), actual_peer_address)
                  .is_ok());
}

TEST(ConnectionCreatorRouteIntegrityIntegration, DirectSocketRejectsUnexpectedLoopbackEndpoint) {
  auto listener = Ipv4LoopbackListener::create();
  ASSERT_TRUE(listener.is_ok());

  auto socket_result = td::SocketFd::open(ipv4_address("127.0.0.1", listener.ok().port()));
  ASSERT_TRUE(socket_result.is_ok());

  auto socket_fd = socket_result.move_as_ok();
  td::IPAddress actual_peer_address;
  ASSERT_TRUE(actual_peer_address.init_peer_address(socket_fd).is_ok());

  ASSERT_TRUE(td::ConnectionCreator::verify_connection_peer(
                  td::Proxy(), ipv4_address("127.0.0.2", listener.ok().port()), actual_peer_address)
                  .is_error());
}

}  // namespace

#endif  // TD_PORT_POSIX