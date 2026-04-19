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

TEST(ConnectionCreatorIpPreferenceContract, UserPreferenceTrueAlwaysEnablesIpv6Preference) {
  auto direct = td::ConnectionCreator::should_prefer_ipv6_for_dc_options(td::Proxy(), true, td::IPAddress());
  ASSERT_TRUE(direct);

  auto proxied = td::ConnectionCreator::should_prefer_ipv6_for_dc_options(
      td::Proxy::socks5("localhost", 1080, "user", "password"), true, ipv4_address("127.0.0.1", 1080));
  ASSERT_TRUE(proxied);
}

TEST(ConnectionCreatorIpPreferenceContract, UserPreferenceFalseKeepsIpv6DisabledForDcSelection) {
  auto direct = td::ConnectionCreator::should_prefer_ipv6_for_dc_options(td::Proxy(), false, td::IPAddress());
  ASSERT_FALSE(direct);

  auto socks5_v6 = td::ConnectionCreator::should_prefer_ipv6_for_dc_options(
      td::Proxy::socks5("localhost", 1080, "user", "password"), false, ipv6_address("::1", 1080));
  ASSERT_FALSE(socks5_v6);

  auto http_v6 = td::ConnectionCreator::should_prefer_ipv6_for_dc_options(
      td::Proxy::http_tcp("localhost", 8080, "user", "password"), false, ipv6_address("::1", 8080));
  ASSERT_FALSE(http_v6);

  auto mtproto_v6 = td::ConnectionCreator::should_prefer_ipv6_for_dc_options(
      td::Proxy::mtproto("localhost", 443, td::mtproto::ProxySecret::from_raw("0123456789abcdef")), false,
      ipv6_address("::1", 443));
  ASSERT_FALSE(mtproto_v6);
}

TEST(ConnectionCreatorIpPreferenceContract, InvalidResolvedProxyAddressDoesNotChangeDecision) {
  auto decision = td::ConnectionCreator::should_prefer_ipv6_for_dc_options(
      td::Proxy::socks5("localhost", 1080, "user", "password"), false, td::IPAddress());
  ASSERT_FALSE(decision);
}

}  // namespace
