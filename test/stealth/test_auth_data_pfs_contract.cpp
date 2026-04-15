// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/AuthData.h"

#include "td/telegram/net/NetReliabilityMonitor.h"

#include "td/utils/ScopeGuard.h"
#include "td/utils/tests.h"

namespace {

TEST(AuthDataPfsContract, ProductionSetterKeepsProtectedMode) {
  td::net_health::reset_net_monitor_for_tests();
  td::mtproto::AuthData::set_legacy_session_mode_for_tests(false);
  SCOPE_EXIT {
    td::mtproto::AuthData::set_legacy_session_mode_for_tests(true);
  };

  td::mtproto::AuthData auth_data;
  auth_data.set_session_mode(false);

  auto snapshot = td::net_health::get_net_monitor_snapshot();
  ASSERT_TRUE(auth_data.is_keyed_session());
  ASSERT_EQ(1u, snapshot.counters.session_param_coerce_attempt_total);
}

TEST(AuthDataPfsContract, ProtectedSetterPreservesProtectedMode) {
  td::net_health::reset_net_monitor_for_tests();
  td::mtproto::AuthData::set_legacy_session_mode_for_tests(false);
  SCOPE_EXIT {
    td::mtproto::AuthData::set_legacy_session_mode_for_tests(true);
  };

  td::mtproto::AuthData auth_data;
  auth_data.set_session_mode(true);

  auto snapshot = td::net_health::get_net_monitor_snapshot();
  ASSERT_TRUE(auth_data.is_keyed_session());
  ASSERT_EQ(0u, snapshot.counters.session_param_coerce_attempt_total);
}

TEST(AuthDataPfsContract, ExplicitTestOverrideAllowsLegacyFixtureMode) {
  td::net_health::reset_net_monitor_for_tests();
  td::mtproto::AuthData::set_legacy_session_mode_for_tests(true);
  SCOPE_EXIT {
    td::mtproto::AuthData::set_legacy_session_mode_for_tests(true);
  };

  td::mtproto::AuthData auth_data;
  auth_data.set_session_mode(false);

  auto snapshot = td::net_health::get_net_monitor_snapshot();
  ASSERT_FALSE(auth_data.is_keyed_session());
  ASSERT_EQ(0u, snapshot.counters.session_param_coerce_attempt_total);
}

TEST(AuthDataPfsContract, TrustedSessionPolicyAllowsExplicitNonPfsModeWithoutTamperCounter) {
  td::net_health::reset_net_monitor_for_tests();
  td::mtproto::AuthData::set_legacy_session_mode_for_tests(false);
  SCOPE_EXIT {
    td::mtproto::AuthData::set_legacy_session_mode_for_tests(true);
  };

  td::mtproto::AuthData auth_data;
  auth_data.set_session_mode_from_policy(false);

  auto snapshot = td::net_health::get_net_monitor_snapshot();
  ASSERT_FALSE(auth_data.is_keyed_session());
  ASSERT_EQ(0u, snapshot.counters.session_param_coerce_attempt_total);
}

}  // namespace