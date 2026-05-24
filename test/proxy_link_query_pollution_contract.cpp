// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/proxy_link_query_pollution_test_utils.h"

namespace {

using td::proxy_link_query_pollution_test::is_mtproto_proxy;
using td::proxy_link_query_pollution_test::is_socks5_proxy;

constexpr auto kValidHexSecret = "1234567890abcdef1234567890ABCDEF";

TEST(ProxyLinkQueryPollutionContract, CanonicalMtprotoProxyLinksRemainSupportedAcrossSchemes) {
  ASSERT_TRUE(is_mtproto_proxy("t.me/proxy?server=google.com&port=80&secret=1234567890abcdef1234567890ABCDEF",
                               "google.com", 80, "1234567890abcdef1234567890abcdef"));
  ASSERT_TRUE(is_mtproto_proxy("tg:proxy?server=google.com&port=80&secret=1234567890abcdef1234567890ABCDEF",
                               "google.com", 80, "1234567890abcdef1234567890abcdef"));
}

TEST(ProxyLinkQueryPollutionContract, CanonicalSocksProxyLinksRemainSupportedAcrossSchemes) {
  ASSERT_TRUE(
      is_socks5_proxy("t.me/socks?server=google.com&port=80&user=user&pass=pass", "google.com", 80, "user", "pass"));
  ASSERT_TRUE(
      is_socks5_proxy("tg:socks?server=google.com&port=80&user=user&pass=pass", "google.com", 80, "user", "pass"));
}

TEST(ProxyLinkQueryPollutionContract, PercentEncodedPortDigitsRemainSupportedForCanonicalLinks) {
  ASSERT_TRUE(is_mtproto_proxy("t.me/proxy?server=google.com&port=8%30&secret=1234567890abcdef1234567890ABCDEF",
                               "google.com", 80, "1234567890abcdef1234567890abcdef"));
  ASSERT_TRUE(
      is_socks5_proxy("tg:socks?server=google.com&port=8%30&user=user&pass=pass", "google.com", 80, "user", "pass"));
}

}  // namespace
