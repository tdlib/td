// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

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

TEST(ConnectionCreatorIpPreferenceIntegration, UserFalseRemainsFalseAcrossProxyFamiliesAndRetryLikeAddressFlips) {
  const auto socks5_proxy = td::Proxy::socks5("proxy.example", 1080, "user", "password");
  const auto http_proxy = td::Proxy::http_tcp("proxy.example", 8080, "user", "password");
  const auto mtproto_proxy =
      td::Proxy::mtproto("proxy.example", 443, td::mtproto::ProxySecret::from_raw("0123456789abcdef"));

  const td::IPAddress resolved_candidates[] = {ipv4_address("203.0.113.10", 1080), ipv6_address("2001:db8::10", 1080),
                                               ipv4_address("198.51.100.7", 8080), ipv6_address("2001:db8::7", 8080),
                                               td::IPAddress()};

  for (const auto &resolved : resolved_candidates) {
    ASSERT_FALSE(td::ConnectionCreator::should_prefer_ipv6_for_dc_options(socks5_proxy, false, resolved));
    ASSERT_FALSE(td::ConnectionCreator::should_prefer_ipv6_for_dc_options(http_proxy, false, resolved));
    ASSERT_FALSE(td::ConnectionCreator::should_prefer_ipv6_for_dc_options(mtproto_proxy, false, resolved));
    ASSERT_FALSE(td::ConnectionCreator::should_prefer_ipv6_for_dc_options(td::Proxy(), false, resolved));
  }
}

TEST(ConnectionCreatorIpPreferenceIntegration, UserTrueRemainsTrueAcrossProxyFamiliesAndRetryLikeAddressFlips) {
  const auto socks5_proxy = td::Proxy::socks5("proxy.example", 1080, "user", "password");
  const auto http_proxy = td::Proxy::http_tcp("proxy.example", 8080, "user", "password");
  const auto mtproto_proxy =
      td::Proxy::mtproto("proxy.example", 443, td::mtproto::ProxySecret::from_raw("0123456789abcdef"));

  const td::IPAddress resolved_candidates[] = {ipv4_address("203.0.113.10", 1080), ipv6_address("2001:db8::10", 1080),
                                               ipv4_address("198.51.100.7", 8080), ipv6_address("2001:db8::7", 8080),
                                               td::IPAddress()};

  for (const auto &resolved : resolved_candidates) {
    ASSERT_TRUE(td::ConnectionCreator::should_prefer_ipv6_for_dc_options(socks5_proxy, true, resolved));
    ASSERT_TRUE(td::ConnectionCreator::should_prefer_ipv6_for_dc_options(http_proxy, true, resolved));
    ASSERT_TRUE(td::ConnectionCreator::should_prefer_ipv6_for_dc_options(mtproto_proxy, true, resolved));
    ASSERT_TRUE(td::ConnectionCreator::should_prefer_ipv6_for_dc_options(td::Proxy(), true, resolved));
  }
}

TEST(ConnectionCreatorIpPreferenceIntegration, DecisionIsPureAndDeterministicAcrossLongAlternatingSequence) {
  const auto proxy = td::Proxy::socks5("proxy.example", 1080, "user", "password");

  for (td::int32 i = 0; i < 4096; ++i) {
    const bool user_prefer_ipv6 = (i % 5) == 0;
    const auto resolved = (i % 2) == 0 ? ipv4_address("127.0.0.1", 1080) : ipv6_address("::1", 1080);

    auto first = td::ConnectionCreator::should_prefer_ipv6_for_dc_options(proxy, user_prefer_ipv6, resolved);
    auto second = td::ConnectionCreator::should_prefer_ipv6_for_dc_options(proxy, user_prefer_ipv6, resolved);

    ASSERT_EQ(first, second);
    ASSERT_EQ(first, user_prefer_ipv6);
  }
}

}  // namespace
