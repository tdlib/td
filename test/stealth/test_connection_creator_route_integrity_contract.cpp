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

td::IPAddress ipv6_mapped_ipv4_address(td::CSlice ip, td::int32 port) {
  td::IPAddress result;
  result.init_ipv6_as_ipv4_port(ip, port).ensure();
  return result;
}

TEST(ConnectionCreatorRouteIntegrityContract, DirectIpv4RouteAcceptsExpectedEndpoint) {
  ASSERT_TRUE(td::ConnectionCreator::verify_connection_peer(td::Proxy(), ipv4_address("149.154.167.50", 443),
                                                            ipv4_address("149.154.167.50", 443))
                  .is_ok());
}

TEST(ConnectionCreatorRouteIntegrityContract, DirectIpv6RouteAcceptsExpectedEndpoint) {
  ASSERT_TRUE(td::ConnectionCreator::verify_connection_peer(td::Proxy(), ipv6_address("2001:67c:4e8:f002::a", 443),
                                                            ipv6_address("2001:67c:4e8:f002::a", 443))
                  .is_ok());
}

TEST(ConnectionCreatorRouteIntegrityContract, MappedRouteNormalizationAcceptsEquivalentEndpoint) {
  ASSERT_TRUE(td::ConnectionCreator::verify_connection_peer(td::Proxy(), ipv4_address("149.154.167.51", 443),
                                                            ipv6_mapped_ipv4_address("149.154.167.51", 443))
                  .is_ok());
}

TEST(ConnectionCreatorRouteIntegrityContract, IntermediatedRoutesBypassEndpointCheck) {
  ASSERT_TRUE(td::ConnectionCreator::verify_connection_peer(
                  td::Proxy::socks5("proxy.example", 1080, "user", "password"), td::IPAddress(), td::IPAddress())
                  .is_ok());
  ASSERT_TRUE(td::ConnectionCreator::verify_connection_peer(
                  td::Proxy::http_tcp("proxy.example", 8080, "user", "password"), td::IPAddress(), td::IPAddress())
                  .is_ok());
  ASSERT_TRUE(td::ConnectionCreator::verify_connection_peer(
                  td::Proxy::mtproto("proxy.example", 443, td::mtproto::ProxySecret::from_raw("0123456789abcdef")),
                  td::IPAddress(), td::IPAddress())
                  .is_ok());
}

}  // namespace