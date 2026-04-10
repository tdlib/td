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
using td::StealthConnectionCountPlan;

TEST(StealthConnectionCountOverflowEdges, SaturatesTotalSessionCountOnAdversarialManualPlanOverflow) {
  StealthConnectionCountPlan plan;
  plan.main_session_count = std::numeric_limits<int32>::max();
  plan.upload_session_count = std::numeric_limits<int32>::max();
  plan.download_session_count = std::numeric_limits<int32>::max();
  plan.download_small_session_count = std::numeric_limits<int32>::max();

  ASSERT_EQ(std::numeric_limits<int32>::max(), plan.total_session_count());
}

TEST(StealthConnectionCountOverflowEdges, SaturatesTotalTcpConnectionCountWhenDoublingWouldOverflow) {
  StealthConnectionCountPlan plan;
  plan.main_session_count = std::numeric_limits<int32>::max();
  plan.upload_session_count = 1;
  plan.download_session_count = 1;
  plan.download_small_session_count = 1;

  ASSERT_EQ(std::numeric_limits<int32>::max(), plan.total_session_count());
  ASSERT_EQ(std::numeric_limits<int32>::max(), plan.total_tcp_connection_count());
}

}  // namespace