//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/port/config.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

#if !TD_WINDOWS
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

namespace td {

Result<string> idn_to_ascii(CSlice host);

class SocketFd;

class IPAddress {
 public:
  IPAddress();

  bool is_valid() const;
  bool is_ipv4() const;
  bool is_ipv6() const;

  int get_port() const;
  void set_port(int port);

  uint32 get_ipv4() const;
  Slice get_ipv6() const;
  Slice get_ip_str() const;

  static CSlice ipv4_to_str(int32 ipv4);

  IPAddress get_any_addr() const;

  Status init_ipv6_port(CSlice ipv6, int port) TD_WARN_UNUSED_RESULT;
  Status init_ipv6_as_ipv4_port(CSlice ipv4, int port) TD_WARN_UNUSED_RESULT;
  Status init_ipv4_port(CSlice ipv4, int port) TD_WARN_UNUSED_RESULT;
  Status init_host_port(CSlice host, int port, bool prefer_ipv6 = false) TD_WARN_UNUSED_RESULT;
  Status init_host_port(CSlice host, CSlice port, bool prefer_ipv6 = false) TD_WARN_UNUSED_RESULT;
  Status init_host_port(CSlice host_port) TD_WARN_UNUSED_RESULT;
  Status init_socket_address(const SocketFd &socket_fd) TD_WARN_UNUSED_RESULT;
  Status init_peer_address(const SocketFd &socket_fd) TD_WARN_UNUSED_RESULT;

  friend bool operator==(const IPAddress &a, const IPAddress &b);
  friend bool operator<(const IPAddress &a, const IPAddress &b);

  // for internal usage only
  const sockaddr *get_sockaddr() const;
  size_t get_sockaddr_len() const;
  int get_address_family() const;

 private:
  union {
    sockaddr_storage addr_;
    sockaddr sockaddr_;
    sockaddr_in ipv4_addr_;
    sockaddr_in6 ipv6_addr_;
  };
  bool is_valid_;

  Status init_sockaddr(sockaddr *addr, socklen_t len) TD_WARN_UNUSED_RESULT;
  void init_ipv4_any();
  void init_ipv6_any();
};

StringBuilder &operator<<(StringBuilder &builder, const IPAddress &address);

}  // namespace td
