// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/StealthConnectionCountPolicy.h"

#include "td/utils/tests.h"

#include <limits>

namespace {

using td::int32;
using td::make_connection_count_plan;
using td::Proxy;
using td::StealthConnectionCountPlan;

TEST(StealthConnectionCountHardening, LegacyPlanClampsMainSessionCountToSessionProxyLimitForNonPremium) {
  auto plan = make_connection_count_plan(Proxy(), std::numeric_limits<int32>::max(), 5, false);

  ASSERT_EQ(100, plan.main_session_count);
  ASSERT_EQ(8, plan.upload_session_count);
  ASSERT_EQ(2, plan.download_session_count);
  ASSERT_EQ(2, plan.download_small_session_count);
  ASSERT_EQ(224, plan.total_tcp_connection_count());
}

TEST(StealthConnectionCountHardening, LegacyPlanClampsMainSessionCountToSessionProxyLimitForPremium) {
  auto plan = make_connection_count_plan(Proxy(), 1000, 5, true);

  ASSERT_EQ(100, plan.main_session_count);
  ASSERT_EQ(8, plan.upload_session_count);
  ASSERT_EQ(8, plan.download_session_count);
  ASSERT_EQ(8, plan.download_small_session_count);
  ASSERT_EQ(248, plan.total_tcp_connection_count());
}

TEST(StealthConnectionCountHardening, LegacyPlanClampsNegativeMainSessionCountToOne) {
  auto plan = make_connection_count_plan(Proxy(), -50, 2, false);

  ASSERT_EQ(1, plan.main_session_count);
  ASSERT_EQ(4, plan.upload_session_count);
  ASSERT_EQ(2, plan.download_session_count);
  ASSERT_EQ(2, plan.download_small_session_count);
  ASSERT_EQ(18, plan.total_tcp_connection_count());
}

TEST(StealthConnectionCountHardening, AdversarialManualPlanCannotDriveNegativeConnectionTotals) {
  StealthConnectionCountPlan plan;
  plan.main_session_count = -7;
  plan.upload_session_count = 2;
  plan.download_session_count = -1;
  plan.download_small_session_count = 3;

  ASSERT_EQ(5, plan.total_session_count());
  ASSERT_EQ(10, plan.total_tcp_connection_count());
}

}  // namespace