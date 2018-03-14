//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/IPAddress.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/port/thread_local.h"
#include "td/utils/ScopeGuard.h"

#if !TD_WINDOWS
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

#include <cstring>

namespace td {

IPAddress::IPAddress() : is_valid_(false) {
}

bool IPAddress::is_valid() const {
  return is_valid_;
}

const sockaddr *IPAddress::get_sockaddr() const {
  return &sockaddr_;
}

size_t IPAddress::get_sockaddr_len() const {
  CHECK(is_valid());
  switch (addr_.ss_family) {
    case AF_INET6:
      return sizeof(ipv6_addr_);
    case AF_INET:
      return sizeof(ipv4_addr_);
    default:
      LOG(FATAL) << "Unknown address family";
      return 0;
  }
}

int IPAddress::get_address_family() const {
  return get_sockaddr()->sa_family;
}

bool IPAddress::is_ipv4() const {
  return get_address_family() == AF_INET;
}

uint32 IPAddress::get_ipv4() const {
  CHECK(is_valid());
  CHECK(is_ipv4());
  return ipv4_addr_.sin_addr.s_addr;
}

Slice IPAddress::get_ipv6() const {
  static_assert(sizeof(ipv6_addr_.sin6_addr) == 16, "ipv6 size == 16");
  CHECK(is_valid());
  CHECK(!is_ipv4());
  return Slice(ipv6_addr_.sin6_addr.s6_addr, 16);
}

IPAddress IPAddress::get_any_addr() const {
  IPAddress res;
  switch (get_address_family()) {
    case AF_INET6:
      res.init_ipv6_any();
      break;
    case AF_INET:
      res.init_ipv4_any();
      break;
    default:
      LOG(FATAL) << "Unknown address family";
  }
  return res;
}
void IPAddress::init_ipv4_any() {
  is_valid_ = true;
  ipv4_addr_.sin_family = AF_INET;
  ipv4_addr_.sin_addr.s_addr = INADDR_ANY;
  ipv4_addr_.sin_port = 0;
}
void IPAddress::init_ipv6_any() {
  is_valid_ = true;
  ipv6_addr_.sin6_family = AF_INET6;
  ipv6_addr_.sin6_addr = in6addr_any;
  ipv6_addr_.sin6_port = 0;
}

Status IPAddress::init_ipv6_port(CSlice ipv6, int port) {
  is_valid_ = false;
  if (port <= 0 || port >= (1 << 16)) {
    return Status::Error(PSLICE() << "Invalid [port=" << port << "]");
  }
  std::memset(&ipv6_addr_, 0, sizeof(ipv6_addr_));
  ipv6_addr_.sin6_family = AF_INET6;
  ipv6_addr_.sin6_port = htons(static_cast<uint16>(port));
  int err = inet_pton(AF_INET6, ipv6.c_str(), &ipv6_addr_.sin6_addr);
  if (err == 0) {
    return Status::Error(PSLICE() << "Failed inet_pton(AF_INET6, " << ipv6 << ")");
  } else if (err == -1) {
    return OS_SOCKET_ERROR(PSLICE() << "Failed inet_pton(AF_INET6, " << ipv6 << ")");
  }
  is_valid_ = true;
  return Status::OK();
}

Status IPAddress::init_ipv6_as_ipv4_port(CSlice ipv4, int port) {
  return init_ipv6_port(string("::FFFF:").append(ipv4.begin(), ipv4.size()), port);
}

Status IPAddress::init_ipv4_port(CSlice ipv4, int port) {
  is_valid_ = false;
  if (port <= 0 || port >= (1 << 16)) {
    return Status::Error(PSLICE() << "Invalid [port=" << port << "]");
  }
  std::memset(&ipv4_addr_, 0, sizeof(ipv4_addr_));
  ipv4_addr_.sin_family = AF_INET;
  ipv4_addr_.sin_port = htons(static_cast<uint16>(port));
  int err = inet_pton(AF_INET, ipv4.c_str(), &ipv4_addr_.sin_addr);
  if (err == 0) {
    return Status::Error(PSLICE() << "Failed inet_pton(AF_INET, " << ipv4 << ")");
  } else if (err == -1) {
    return OS_SOCKET_ERROR(PSLICE() << "Failed inet_pton(AF_INET, " << ipv4 << ")");
  }
  is_valid_ = true;
  return Status::OK();
}

Status IPAddress::init_host_port(CSlice host, int port) {
  auto str_port = to_string(port);
  return init_host_port(host, str_port);
}

Status IPAddress::init_host_port(CSlice host, CSlice port) {
  addrinfo hints;
  addrinfo *info = nullptr;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;  // TODO AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  LOG(INFO) << "Try to init IP address of " << host << " with port " << port;
  auto s = getaddrinfo(host.c_str(), port.c_str(), &hints, &info);
  if (s != 0) {
    return Status::Error(PSLICE() << "getaddrinfo: " << gai_strerror(s));
  }
  SCOPE_EXIT {
    freeaddrinfo(info);
  };

  // prefer ipv4
  addrinfo *best_info = info;
  for (auto *ptr = info->ai_next; ptr != nullptr; ptr = ptr->ai_next) {
    if (ptr->ai_socktype == AF_INET) {
      best_info = ptr;
      break;
    }
  }
  // just use first address
  CHECK(best_info != nullptr);
  return init_sockaddr(best_info->ai_addr, narrow_cast<socklen_t>(best_info->ai_addrlen));
}

Status IPAddress::init_host_port(CSlice host_port) {
  auto pos = host_port.rfind(':');
  if (pos == static_cast<size_t>(-1)) {
    return Status::Error("Can't split string into host and port");
  }
  return init_host_port(host_port.substr(0, pos).str(), host_port.substr(pos + 1).str());
}

Status IPAddress::init_sockaddr(sockaddr *addr, socklen_t len) {
  if (addr->sa_family == AF_INET6) {
    CHECK(len == sizeof(ipv6_addr_));
    std::memcpy(&ipv6_addr_, reinterpret_cast<sockaddr_in6 *>(addr), sizeof(ipv6_addr_));
    LOG(INFO) << "Have ipv6 address " << get_ip_str() << " with port " << get_port();
  } else if (addr->sa_family == AF_INET) {
    CHECK(len == sizeof(ipv4_addr_));
    std::memcpy(&ipv4_addr_, reinterpret_cast<sockaddr_in *>(addr), sizeof(ipv4_addr_));
    LOG(INFO) << "Have ipv4 address " << get_ip_str() << " with port " << get_port();
  } else {
    return Status::Error(PSLICE() << "Unknown " << tag("sa_family", addr->sa_family));
  }

  is_valid_ = true;
  return Status::OK();
}

Status IPAddress::init_socket_address(const SocketFd &socket_fd) {
  is_valid_ = false;
#if TD_WINDOWS
  auto fd = socket_fd.get_fd().get_native_socket();
#else
  auto fd = socket_fd.get_fd().get_native_fd();
#endif
  socklen_t len = sizeof(addr_);
  int ret = getsockname(fd, &sockaddr_, &len);
  if (ret != 0) {
    return OS_SOCKET_ERROR("Failed to get socket address");
  }
  is_valid_ = true;
  return Status::OK();
}

Status IPAddress::init_peer_address(const SocketFd &socket_fd) {
  is_valid_ = false;
#if TD_WINDOWS
  auto fd = socket_fd.get_fd().get_native_socket();
#else
  auto fd = socket_fd.get_fd().get_native_fd();
#endif
  socklen_t len = sizeof(addr_);
  int ret = getpeername(fd, &sockaddr_, &len);
  if (ret != 0) {
    return OS_SOCKET_ERROR("Failed to get peer socket address");
  }
  is_valid_ = true;
  return Status::OK();
}

static CSlice get_ip_str(int family, const void *addr) {
  const int buf_size = INET6_ADDRSTRLEN;  //, INET_ADDRSTRLEN;
  static TD_THREAD_LOCAL char *buf;
  init_thread_local<char[]>(buf, buf_size);

  const char *res = inet_ntop(family,
#if TD_WINDOWS
                              const_cast<PVOID>(addr),
#else
                              addr,
#endif
                              buf, buf_size);
  if (res == nullptr) {
    return CSlice();
  } else {
    return CSlice(res);
  }
}

CSlice IPAddress::ipv4_to_str(int32 ipv4) {
  auto tmp_ipv4 = ntohl(ipv4);
  return ::td::get_ip_str(AF_INET, &tmp_ipv4);
}

Slice IPAddress::get_ip_str() const {
  if (!is_valid()) {
    return Slice("0.0.0.0");
  }

  const void *addr;
  switch (get_address_family()) {
    case AF_INET6:
      addr = &ipv6_addr_.sin6_addr;
      break;
    case AF_INET:
      addr = &ipv4_addr_.sin_addr;
      break;
    default:
      UNREACHABLE();
      return Slice();
  }
  return ::td::get_ip_str(get_address_family(), addr);
}

int IPAddress::get_port() const {
  if (!is_valid()) {
    return 0;
  }

  switch (get_address_family()) {
    case AF_INET6:
      return ntohs(ipv6_addr_.sin6_port);
    case AF_INET:
      return ntohs(ipv4_addr_.sin_port);
    default:
      UNREACHABLE();
      return 0;
  }
}

void IPAddress::set_port(int port) {
  CHECK(is_valid());

  switch (get_address_family()) {
    case AF_INET6:
      ipv6_addr_.sin6_port = htons(static_cast<uint16>(port));
      break;
    case AF_INET:
      ipv4_addr_.sin_port = htons(static_cast<uint16>(port));
      break;
    default:
      UNREACHABLE();
  }
}

bool operator==(const IPAddress &a, const IPAddress &b) {
  if (!a.is_valid() || !b.is_valid()) {
    return false;
  }
  if (a.get_address_family() != b.get_address_family()) {
    return false;
  }

  if (a.get_address_family() == AF_INET) {
    return a.ipv4_addr_.sin_port == b.ipv4_addr_.sin_port &&
           std::memcmp(&a.ipv4_addr_.sin_addr, &b.ipv4_addr_.sin_addr, sizeof(a.ipv4_addr_.sin_addr)) == 0;
  } else if (a.get_address_family() == AF_INET6) {
    return a.ipv6_addr_.sin6_port == b.ipv6_addr_.sin6_port &&
           std::memcmp(&a.ipv6_addr_.sin6_addr, &b.ipv6_addr_.sin6_addr, sizeof(a.ipv6_addr_.sin6_addr)) == 0;
  }

  LOG(FATAL) << "Unknown address family";
  return false;
}

bool operator<(const IPAddress &a, const IPAddress &b) {
  if (a.is_valid() != b.is_valid()) {
    return a.is_valid() < b.is_valid();
  }
  if (a.get_address_family() != b.get_address_family()) {
    return a.get_address_family() < b.get_address_family();
  }

  if (a.get_address_family() == AF_INET) {
    if (a.ipv4_addr_.sin_port != b.ipv4_addr_.sin_port) {
      return a.ipv4_addr_.sin_port < b.ipv4_addr_.sin_port;
    }
    return std::memcmp(&a.ipv4_addr_.sin_addr, &b.ipv4_addr_.sin_addr, sizeof(a.ipv4_addr_.sin_addr)) < 0;
  } else if (a.get_address_family() == AF_INET6) {
    if (a.ipv6_addr_.sin6_port != b.ipv6_addr_.sin6_port) {
      return a.ipv6_addr_.sin6_port < b.ipv6_addr_.sin6_port;
    }
    return std::memcmp(&a.ipv6_addr_.sin6_addr, &b.ipv6_addr_.sin6_addr, sizeof(a.ipv6_addr_.sin6_addr)) < 0;
  }

  LOG(FATAL) << "Unknown address family";
  return false;
}

StringBuilder &operator<<(StringBuilder &builder, const IPAddress &address) {
  if (!address.is_valid()) {
    return builder << "[invalid]";
  }
  if (address.get_address_family() == AF_INET) {
    return builder << "[" << address.get_ip_str() << ":" << address.get_port() << "]";
  } else {
    CHECK(address.get_address_family() == AF_INET6);
    return builder << "[[" << address.get_ip_str() << "]:" << address.get_port() << "]";
  }
}

}  // namespace td
