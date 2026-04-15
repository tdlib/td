// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/NetReliabilityMonitor.h"

#include "td/utils/tests.h"

namespace {

TEST(NetReliabilityMonitorContract, ResetStartsHealthy) {
  td::net_health::reset_net_monitor_for_tests();

  auto snapshot = td::net_health::get_net_monitor_snapshot();
  ASSERT_TRUE(snapshot.state == td::net_health::NetMonitorState::Healthy);
  ASSERT_EQ(0u, snapshot.counters.session_param_coerce_attempt_total);
  ASSERT_EQ(0u, snapshot.counters.auth_key_destroy_total);
}

TEST(NetReliabilityMonitorContract, ProtectedModeTamperAttemptPromotesSuspiciousState) {
  td::net_health::reset_net_monitor_for_tests();

  td::net_health::note_session_param_coerce_attempt();

  auto snapshot = td::net_health::get_net_monitor_snapshot();
  ASSERT_TRUE(snapshot.state == td::net_health::NetMonitorState::Suspicious);
  ASSERT_EQ(1u, snapshot.counters.session_param_coerce_attempt_total);
}

TEST(NetReliabilityMonitorContract, SingleDestroyDefersReauthenticationForSameDc) {
  td::net_health::reset_net_monitor_for_tests();

  td::net_health::note_auth_key_destroy(2, td::net_health::AuthKeyDestroyReason::ProgrammaticApiCall, 100.0);

  auto snapshot = td::net_health::get_net_monitor_snapshot();
  ASSERT_TRUE(snapshot.state == td::net_health::NetMonitorState::Degraded);
  ASSERT_EQ(1u, snapshot.counters.auth_key_destroy_total);
  ASSERT_TRUE(td::net_health::get_reauth_not_before(2) >= 102.0);
}

}  // namespace