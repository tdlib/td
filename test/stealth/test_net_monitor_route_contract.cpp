// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/telegram/net/NetReliabilityMonitor.h"

#include "td/utils/tests.h"

namespace {

TEST(NetMonitorRouteContract, AcceptedMainRouteChangeStaysObservableWithoutEscalation) {
  td::net_health::reset_net_monitor_for_tests();

  td::net_health::note_main_dc_migration(true, false);

  auto snapshot = td::net_health::get_net_monitor_snapshot();
  ASSERT_EQ(1u, snapshot.counters.main_dc_migration_accept_total);
  ASSERT_EQ(0u, snapshot.counters.main_dc_migration_reject_total);
  ASSERT_TRUE(snapshot.state == td::net_health::NetMonitorState::Healthy);
}

TEST(NetMonitorRouteContract, RejectedMainRouteChangeEscalatesMonitorState) {
  td::net_health::reset_net_monitor_for_tests();

  td::net_health::note_main_dc_migration(false, false);

  auto snapshot = td::net_health::get_net_monitor_snapshot();
  ASSERT_EQ(0u, snapshot.counters.main_dc_migration_accept_total);
  ASSERT_EQ(1u, snapshot.counters.main_dc_migration_reject_total);
  ASSERT_EQ(0u, snapshot.counters.main_dc_migration_rate_limit_total);
  ASSERT_TRUE(snapshot.state == td::net_health::NetMonitorState::Suspicious);
}

TEST(NetMonitorRouteContract, RateLimitedRouteChangeRecordsDedicatedCounter) {
  td::net_health::reset_net_monitor_for_tests();

  td::net_health::note_main_dc_migration(false, true);

  auto snapshot = td::net_health::get_net_monitor_snapshot();
  ASSERT_EQ(1u, snapshot.counters.main_dc_migration_reject_total);
  ASSERT_EQ(1u, snapshot.counters.main_dc_migration_rate_limit_total);
  ASSERT_TRUE(snapshot.state == td::net_health::NetMonitorState::Suspicious);
}

}  // namespace