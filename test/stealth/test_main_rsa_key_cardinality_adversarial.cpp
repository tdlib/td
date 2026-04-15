// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/PublicRsaKeySharedMain.h"

#include "td/telegram/net/NetReliabilityMonitor.h"

#include "td/utils/tests.h"

namespace {

TEST(MainRsaKeyCardinalityAdversarial, ProductionKeySetRejectsOverflow) {
  td::net_health::reset_net_monitor_for_tests();

  ASSERT_TRUE(td::PublicRsaKeySharedMain::validate_key_count(2, false).is_error());

  auto snapshot = td::net_health::get_net_monitor_snapshot();
  ASSERT_EQ(1u, snapshot.counters.main_key_set_cardinality_failure_total);
  ASSERT_TRUE(snapshot.state == td::net_health::NetMonitorState::Suspicious);
}

TEST(MainRsaKeyCardinalityAdversarial, TestKeySetRejectsMissingKey) {
  td::net_health::reset_net_monitor_for_tests();

  ASSERT_TRUE(td::PublicRsaKeySharedMain::validate_key_count(0, true).is_error());

  auto snapshot = td::net_health::get_net_monitor_snapshot();
  ASSERT_EQ(1u, snapshot.counters.main_key_set_cardinality_failure_total);
  ASSERT_TRUE(snapshot.state == td::net_health::NetMonitorState::Suspicious);
}

}  // namespace