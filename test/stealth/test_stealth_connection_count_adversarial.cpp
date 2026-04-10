// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/StealthConnectionCountPolicy.h"

#include "td/utils/tests.h"

namespace {

using td::make_connection_count_plan;
using td::Proxy;

td::mtproto::ProxySecret make_tls_proxy_secret() {
  td::string secret;
  secret.push_back(static_cast<char>(0xee));
  secret += "0123456789abcdef";
  secret += "desktop.example";
  return td::mtproto::ProxySecret::from_raw(secret);
}

TEST(StealthConnectionCountAdversarial, PremiumUserStillStaysAtOrBelowSixSockets) {
  auto plan = make_connection_count_plan(Proxy::mtproto("proxy.example", 443, make_tls_proxy_secret()), 1, 5, true);

  ASSERT_TRUE(plan.capped_for_stealth_tls_proxy);
  ASSERT_TRUE(plan.total_tcp_connection_count() <= 6);
}

TEST(StealthConnectionCountAdversarial, ClassicMtprotoSecretDoesNotTriggerStealthCap) {
  auto plan = make_connection_count_plan(
      Proxy::mtproto("proxy.example", 443, td::mtproto::ProxySecret::from_raw("0123456789abcdef")), 1, 5, true);

  ASSERT_FALSE(plan.capped_for_stealth_tls_proxy);
  ASSERT_EQ(50, plan.total_tcp_connection_count());
}

TEST(StealthConnectionCountAdversarial, LegacyDcTwoUploadSpecialCaseSurvivesOutsideStealth) {
  auto plan = make_connection_count_plan(Proxy(), 1, 2, false);

  ASSERT_FALSE(plan.capped_for_stealth_tls_proxy);
  ASSERT_EQ(4, plan.upload_session_count);
  ASSERT_EQ(2, plan.download_session_count);
  ASSERT_EQ(2, plan.download_small_session_count);
}

TEST(StealthConnectionCountAdversarial, DisablingStealthRestoresLegacyParallelism) {
  auto capped_plan =
      make_connection_count_plan(Proxy::mtproto("proxy.example", 443, make_tls_proxy_secret()), 1, 5, true);
  auto restored_plan = make_connection_count_plan(Proxy(), 1, 5, true);

  ASSERT_EQ(6, capped_plan.total_tcp_connection_count());
  ASSERT_EQ(50, restored_plan.total_tcp_connection_count());
}

}  // namespace