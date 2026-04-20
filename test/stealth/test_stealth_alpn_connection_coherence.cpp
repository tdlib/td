// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/ReviewedClientHelloReferences.h"

#include "td/telegram/net/StealthConnectionCountPolicy.h"

#include "td/utils/tests.h"

namespace {

using td::make_connection_count_plan;
using td::mtproto::test::fixtures::reviewed_refs::chrome146_177_android16_alpn_protocols;
using td::mtproto::test::fixtures::reviewed_refs::chrome146_177_linux_desktop_alpn_protocols;
using td::Proxy;

td::mtproto::ProxySecret make_tls_proxy_secret() {
  td::string secret;
  secret.push_back(static_cast<char>(0xee));
  secret += "0123456789abcdef";
  secret += "fixture.example";
  return td::mtproto::ProxySecret::from_raw(secret);
}

TEST(StealthAlpnConnectionCoherence, MobileProxyFixtureRemainsCoherentWithHttp11SizedCap) {
  ASSERT_EQ(1u, chrome146_177_android16_alpn_protocols.size());
  ASSERT_EQ("http/1.1", chrome146_177_android16_alpn_protocols[0]);

  auto plan = make_connection_count_plan(Proxy::mtproto("proxy.example", 443, make_tls_proxy_secret()), 4, 5, true);

  ASSERT_EQ(6, plan.total_tcp_connection_count());
}

TEST(StealthAlpnConnectionCoherence, DesktopProxyFixtureRemainsCoherentWithFallbackCap) {
  ASSERT_EQ(2u, chrome146_177_linux_desktop_alpn_protocols.size());
  ASSERT_EQ("h2", chrome146_177_linux_desktop_alpn_protocols[0]);
  ASSERT_EQ("http/1.1", chrome146_177_linux_desktop_alpn_protocols[1]);

  auto plan = make_connection_count_plan(Proxy::mtproto("proxy.example", 443, make_tls_proxy_secret()), 4, 5, true);

  ASSERT_TRUE(plan.total_tcp_connection_count() <= 6);
}

TEST(StealthAlpnConnectionCoherence, LegacyDesktopPlanCanStillExceedBrowserNormWithoutStealthCap) {
  ASSERT_EQ(2u, chrome146_177_linux_desktop_alpn_protocols.size());

  auto plan = make_connection_count_plan(Proxy(), 1, 5, true);

  ASSERT_TRUE(plan.total_tcp_connection_count() > 6);
}

}  // namespace