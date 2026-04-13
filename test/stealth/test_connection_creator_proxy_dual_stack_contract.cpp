// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/ConnectionCreator.h"

#include "td/utils/tests.h"

namespace {

td::IPAddress ipv4_address(td::CSlice ip, td::int32 port) {
  td::IPAddress result;
  result.init_ipv4_port(ip, port).ensure();
  return result;
}

td::IPAddress ipv6_address(td::CSlice ip, td::int32 port) {
  td::IPAddress result;
  result.init_ipv6_port(ip, port).ensure();
  return result;
}

TEST(ConnectionCreatorProxyDualStackContract, HostnameIpv6ResolutionAddsIpv4FallbackCandidate) {
  auto candidates = td::ConnectionCreator::resolve_proxy_address_candidates(
      td::Proxy::socks5("localhost", 443, "user", "password"), ipv6_address("::1", 443));
  ASSERT_TRUE(candidates.is_ok());

  ASSERT_TRUE(candidates.ok().primary_ip_address.is_ipv6());
  ASSERT_EQ(candidates.ok().primary_ip_address.get_ip_str(), "::1");
  ASSERT_TRUE(candidates.ok().fallback_ip_address.is_ipv4());
  ASSERT_EQ(candidates.ok().fallback_ip_address.get_ip_str(), "127.0.0.1");
  ASSERT_EQ(candidates.ok().fallback_ip_address.get_port(), 443);
}

TEST(ConnectionCreatorProxyDualStackContract, HostnameIpv4ResolutionKeepsFallbackOppositeFamilyWhenAvailable) {
  auto candidates = td::ConnectionCreator::resolve_proxy_address_candidates(
      td::Proxy::http_tcp("localhost", 8080, "user", "password"), ipv4_address("127.0.0.1", 8080));
  ASSERT_TRUE(candidates.is_ok());

  ASSERT_TRUE(candidates.ok().primary_ip_address.is_ipv4());
  ASSERT_EQ(candidates.ok().primary_ip_address.get_ip_str(), "127.0.0.1");
  if (candidates.ok().fallback_ip_address.is_valid()) {
    ASSERT_TRUE(candidates.ok().fallback_ip_address.is_ipv6());
    ASSERT_EQ(candidates.ok().fallback_ip_address.get_ip_str(), "::1");
    ASSERT_EQ(candidates.ok().fallback_ip_address.get_port(), 8080);
  }
}

TEST(ConnectionCreatorProxyDualStackContract, LiteralIpv4ProxyDoesNotSynthesizeAlternateFamily) {
  auto candidates = td::ConnectionCreator::resolve_proxy_address_candidates(
      td::Proxy::socks5("127.0.0.1", 1080, "user", "password"), ipv4_address("127.0.0.1", 1080));
  ASSERT_TRUE(candidates.is_ok());

  ASSERT_TRUE(candidates.ok().primary_ip_address.is_ipv4());
  ASSERT_FALSE(candidates.ok().fallback_ip_address.is_valid());
}

}  // namespace