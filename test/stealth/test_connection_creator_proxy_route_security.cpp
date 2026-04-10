// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/ConnectionCreator.h"

#include "td/utils/port/IPAddress.h"
#include "td/utils/tests.h"

namespace {

td::IPAddress ipv4_address(td::CSlice ip, td::int32 port) {
  td::IPAddress result;
  result.init_ipv4_port(ip, port).ensure();
  return result;
}

TEST(ConnectionCreatorProxyRouteSecurity, DirectRawIpRouteUsesExplicitTelegramAddress) {
  auto route = td::ConnectionCreator::resolve_raw_ip_connection_route(td::Proxy(), td::IPAddress(),
                                                                      ipv4_address("149.154.167.50", 443));
  ASSERT_TRUE(route.is_ok());

  ASSERT_EQ(route.ok().socket_ip_address.get_ip_str(), "149.154.167.50");
  ASSERT_EQ(route.ok().socket_ip_address.get_port(), 443);
  ASSERT_FALSE(route.ok().mtproto_ip_address.is_valid());
}

TEST(ConnectionCreatorProxyRouteSecurity, MtprotoProxyRawIpRouteUsesProxyAddress) {
  auto route = td::ConnectionCreator::resolve_raw_ip_connection_route(
      td::Proxy::mtproto("proxy.example", 443, td::mtproto::ProxySecret::from_raw("0123456789abcdef")),
      ipv4_address("203.0.113.10", 443), ipv4_address("149.154.167.50", 443));
  ASSERT_TRUE(route.is_ok());

  ASSERT_EQ(route.ok().socket_ip_address.get_ip_str(), "203.0.113.10");
  ASSERT_EQ(route.ok().socket_ip_address.get_port(), 443);
  ASSERT_FALSE(route.ok().mtproto_ip_address.is_valid());
}

TEST(ConnectionCreatorProxyRouteSecurity, Socks5RawIpRouteTunnelsTelegramAddress) {
  auto route = td::ConnectionCreator::resolve_raw_ip_connection_route(
      td::Proxy::socks5("proxy.example", 1080, "user", "password"), ipv4_address("203.0.113.20", 1080),
      ipv4_address("149.154.167.91", 443));
  ASSERT_TRUE(route.is_ok());

  ASSERT_EQ(route.ok().socket_ip_address.get_ip_str(), "203.0.113.20");
  ASSERT_EQ(route.ok().socket_ip_address.get_port(), 1080);
  ASSERT_EQ(route.ok().mtproto_ip_address.get_ip_str(), "149.154.167.91");
  ASSERT_EQ(route.ok().mtproto_ip_address.get_port(), 443);
}

TEST(ConnectionCreatorProxyRouteSecurity, ProxyRawIpRouteFailsClosedWithoutResolvedProxyAddress) {
  auto route = td::ConnectionCreator::resolve_raw_ip_connection_route(
      td::Proxy::mtproto("proxy.example", 443, td::mtproto::ProxySecret::from_raw("0123456789abcdef")), td::IPAddress(),
      ipv4_address("149.154.167.50", 443));
  ASSERT_TRUE(route.is_error());
}

}  // namespace