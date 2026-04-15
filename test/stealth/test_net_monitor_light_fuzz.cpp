// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/NetReliabilityMonitor.h"

#include "td/utils/tests.h"

namespace {

TEST(NetMonitorLightFuzz, ReauthBarrierNeverMovesBackwardAcrossSeedMatrix) {
  for (td::int32 dc_id = 1; dc_id <= 32; dc_id++) {
    td::net_health::reset_net_monitor_for_tests();
    auto expected_barrier = 0.0;

    for (td::uint32 seed = 1; seed <= 64; seed++) {
      auto now = static_cast<double>(seed) * 0.5;
      td::net_health::note_auth_key_destroy(dc_id, td::net_health::AuthKeyDestroyReason::ProgrammaticApiCall,
                                                 now);
      expected_barrier = td::max(expected_barrier, now + 2.0);
      ASSERT_TRUE(td::net_health::get_reauth_not_before(dc_id) >= expected_barrier);
    }
  }
}

TEST(NetMonitorLightFuzz, DistinctDcMatrixOnlyFlagsBurstsWithinWindow) {
  for (td::uint32 seed = 1; seed <= 48; seed++) {
    td::net_health::reset_net_monitor_for_tests();

    auto first_dc_id = static_cast<td::int32>((seed % 5) + 1);
    auto second_dc_id = static_cast<td::int32>(((seed + 1) % 5) + 1);
    auto first_time = static_cast<double>(seed);
    auto second_time = first_time + static_cast<double>(seed % 40);

    td::net_health::note_auth_key_destroy(first_dc_id, td::net_health::AuthKeyDestroyReason::ServerRevoke,
                                               first_time);
    td::net_health::note_auth_key_destroy(second_dc_id, td::net_health::AuthKeyDestroyReason::ServerRevoke,
                                               second_time);

    auto snapshot = td::net_health::get_net_monitor_snapshot();
    auto should_burst = first_dc_id != second_dc_id && second_time <= first_time + 30.0;
    ASSERT_EQ(should_burst ? 1u : 0u, snapshot.counters.auth_key_destroy_burst_total);
  }
}

}  // namespace