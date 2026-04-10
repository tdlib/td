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
  secret += "matrix.example";
  return td::mtproto::ProxySecret::from_raw(secret);
}

TEST(StealthConnectionCountMatrix, TlsStealthCapHoldsAcrossPremiumAndDcMatrix) {
  const auto tls_proxy = Proxy::mtproto("proxy.example", 443, make_tls_proxy_secret());

  for (auto raw_dc_id : {1, 2, 4, 5}) {
    for (auto main_session_count : {1, 2, 8, 32}) {
      for (bool is_premium : {false, true}) {
        auto plan = make_connection_count_plan(tls_proxy, main_session_count, raw_dc_id, is_premium);
        ASSERT_TRUE(plan.capped_for_stealth_tls_proxy);
        ASSERT_EQ(1, plan.main_session_count);
        ASSERT_EQ(1, plan.upload_session_count);
        ASSERT_EQ(1, plan.download_session_count);
        ASSERT_EQ(0, plan.download_small_session_count);
        ASSERT_EQ(3, plan.total_session_count());
        ASSERT_EQ(6, plan.total_tcp_connection_count());
      }
    }
  }
}

TEST(StealthConnectionCountMatrix, LegacyPlanMatchesHistoricalParallelismMatrix) {
  for (auto raw_dc_id : {1, 2, 4, 5}) {
    for (auto main_session_count : {1, 3}) {
      for (bool is_premium : {false, true}) {
        auto plan = make_connection_count_plan(Proxy(), main_session_count, raw_dc_id, is_premium);
        ASSERT_FALSE(plan.capped_for_stealth_tls_proxy);
        ASSERT_EQ(main_session_count, plan.main_session_count);
        ASSERT_EQ(is_premium ? 8 : ((raw_dc_id != 2 && raw_dc_id != 4) ? 8 : 4), plan.upload_session_count);
        ASSERT_EQ(is_premium ? 8 : 2, plan.download_session_count);
        ASSERT_EQ(is_premium ? 8 : 2, plan.download_small_session_count);
      }
    }
  }
}

}  // namespace