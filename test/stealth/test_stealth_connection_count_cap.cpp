// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/StealthConnectionCountPolicy.h"

#include "td/utils/tests.h"

namespace {

using td::make_connection_count_plan;
using td::NetQuery;
using td::Proxy;
using td::resolve_connection_count_routed_query_type;

td::mtproto::ProxySecret make_tls_proxy_secret() {
  td::string secret;
  secret.push_back(static_cast<char>(0xee));
  secret += "0123456789abcdef";
  secret += "proxy.example";
  return td::mtproto::ProxySecret::from_raw(secret);
}

TEST(StealthConnectionCountCap, CapsTlsEmulatingMtprotoProxyToBrowserNorm) {
  auto plan = make_connection_count_plan(Proxy::mtproto("proxy.example", 443, make_tls_proxy_secret()), 8, 5, true);

  ASSERT_TRUE(plan.capped_for_stealth_tls_proxy);
  ASSERT_EQ(1, plan.main_session_count);
  ASSERT_EQ(1, plan.upload_session_count);
  ASSERT_EQ(1, plan.download_session_count);
  ASSERT_EQ(0, plan.download_small_session_count);
  ASSERT_EQ(6, plan.total_tcp_connection_count());
}

TEST(StealthConnectionCountCap, PreservesLegacyCountsWithoutStealthTlsProxy) {
  auto plan = make_connection_count_plan(Proxy(), 3, 2, false);

  ASSERT_FALSE(plan.capped_for_stealth_tls_proxy);
  ASSERT_EQ(3, plan.main_session_count);
  ASSERT_EQ(4, plan.upload_session_count);
  ASSERT_EQ(2, plan.download_session_count);
  ASSERT_EQ(2, plan.download_small_session_count);
  ASSERT_EQ(22, plan.total_tcp_connection_count());
}

TEST(StealthConnectionCountCap, ReroutesDownloadSmallIntoDownloadWhenCapEnabled) {
  auto capped_plan =
      make_connection_count_plan(Proxy::mtproto("proxy.example", 443, make_tls_proxy_secret()), 1, 5, false);
  auto legacy_plan = make_connection_count_plan(Proxy(), 1, 5, false);

  ASSERT_TRUE(resolve_connection_count_routed_query_type(NetQuery::Type::DownloadSmall, capped_plan) ==
              NetQuery::Type::Download);
  ASSERT_TRUE(resolve_connection_count_routed_query_type(NetQuery::Type::DownloadSmall, legacy_plan) ==
              NetQuery::Type::DownloadSmall);
}

TEST(StealthConnectionCountCap, IgnoresConfiguredMainSessionFanoutInStealthMode) {
  auto plan = make_connection_count_plan(Proxy::mtproto("proxy.example", 443, make_tls_proxy_secret()), 32, 4, true);

  ASSERT_EQ(1, plan.main_session_count);
  ASSERT_EQ(6, plan.total_tcp_connection_count());
}

}  // namespace