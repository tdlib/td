//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "td/utils/port/IPAddress.h"

#include "td/utils/Random.h"
#include "td/utils/tests.h"

TEST(IPAddressSecurity, init_sockaddr_accepts_valid_ipv4) {
  td::IPAddress ip_address;

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(443);
  ASSERT_EQ(1, inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr));

  auto status = ip_address.init_sockaddr(reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
  ASSERT_TRUE(status.is_ok());
  ASSERT_TRUE(ip_address.is_valid());
  ASSERT_TRUE(ip_address.is_ipv4());
  ASSERT_EQ(443, ip_address.get_port());
}

TEST(IPAddressSecurity, init_sockaddr_rejects_truncated_ipv4_length) {
  td::IPAddress ip_address;

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(80);
  ASSERT_EQ(1, inet_pton(AF_INET, "10.0.0.1", &addr.sin_addr));

  auto status = ip_address.init_sockaddr(reinterpret_cast<sockaddr *>(&addr), sizeof(addr) - 1);
  ASSERT_TRUE(status.is_error());
  ASSERT_FALSE(ip_address.is_valid());
}

TEST(IPAddressSecurity, init_sockaddr_rejects_truncated_ipv6_length) {
  td::IPAddress ip_address;

  sockaddr_in6 addr{};
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons(443);
  ASSERT_EQ(1, inet_pton(AF_INET6, "::1", &addr.sin6_addr));

  auto status = ip_address.init_sockaddr(reinterpret_cast<sockaddr *>(&addr), sizeof(addr) - 1);
  ASSERT_TRUE(status.is_error());
  ASSERT_FALSE(ip_address.is_valid());
}

TEST(IPAddressSecurity, init_sockaddr_rejects_unknown_family) {
  td::IPAddress ip_address;

  sockaddr addr{};
  addr.sa_family = AF_UNSPEC;

  auto status = ip_address.init_sockaddr(&addr, sizeof(addr));
  ASSERT_TRUE(status.is_error());
  ASSERT_FALSE(ip_address.is_valid());
}

TEST(IPAddressSecurity, init_sockaddr_light_fuzz_fail_closed) {
  constexpr int kIterations = 10000;
  for (int i = 0; i < kIterations; i++) {
    td::IPAddress ip_address;
    sockaddr_storage storage{};
    td::Random::secure_bytes(reinterpret_cast<unsigned char *>(&storage), sizeof(storage));

    auto *addr = reinterpret_cast<sockaddr *>(&storage);
    switch (i % 3) {
      case 0:
        addr->sa_family = AF_INET;
        break;
      case 1:
        addr->sa_family = AF_INET6;
        break;
      default:
        addr->sa_family = AF_UNSPEC;
        break;
    }

    socklen_t len = static_cast<socklen_t>(i % (sizeof(storage) + 1));
    auto status = ip_address.init_sockaddr(addr, len);

    bool must_be_ok = (addr->sa_family == AF_INET && len == sizeof(sockaddr_in)) ||
                      (addr->sa_family == AF_INET6 && len == sizeof(sockaddr_in6));
    if (must_be_ok) {
      ASSERT_TRUE(status.is_ok());
      ASSERT_TRUE(ip_address.is_valid());
    } else {
      ASSERT_TRUE(status.is_error());
      ASSERT_FALSE(ip_address.is_valid());
    }
  }
}