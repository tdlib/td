// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/ConnectionPoolPolicy.h"

#include "td/mtproto/stealth/StealthRuntimeParams.h"

#include "td/utils/tests.h"

#include <cmath>

namespace {

using td::ConnectionPoolPolicy;
using td::mtproto::stealth::default_runtime_stealth_params;

void assert_double_eq(double expected, double actual) {
  ASSERT_TRUE(std::abs(expected - actual) < 1e-9);
}

TEST(ConnectionPoolPolicy, DefaultRetentionWinsWhenRuntimeLifetimeIsLonger) {
  auto params = default_runtime_stealth_params();
  params.flow_behavior.max_conn_lifetime_ms = 60000;

  assert_double_eq(10.0, ConnectionPoolPolicy::pooled_connection_retention_seconds(10.0, params.flow_behavior));
}

TEST(ConnectionPoolPolicy, RuntimeLifetimeCapsRetentionWhenOperatorTightensBudget) {
  auto params = default_runtime_stealth_params();
  params.flow_behavior.max_conn_lifetime_ms = 250;

  assert_double_eq(0.25, ConnectionPoolPolicy::pooled_connection_retention_seconds(10.0, params.flow_behavior));
}

TEST(ConnectionPoolPolicy, ExpiryUsesTheClampedRetentionWindow) {
  auto params = default_runtime_stealth_params();
  params.flow_behavior.max_conn_lifetime_ms = 250;

  ASSERT_FALSE(ConnectionPoolPolicy::is_pooled_connection_expired(10.0, 10.25, 10.0, params.flow_behavior));
  ASSERT_TRUE(ConnectionPoolPolicy::is_pooled_connection_expired(10.0, 10.250001, 10.0, params.flow_behavior));
}

}  // namespace