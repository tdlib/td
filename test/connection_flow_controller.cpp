//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/net/ConnectionFlowController.h"

#include "td/mtproto/stealth/StealthRuntimeParams.h"

#include "td/utils/tests.h"

#include <cmath>

namespace {

using td::ConnectionFlowController;
using td::mtproto::stealth::default_runtime_stealth_params;

void assert_double_eq(double expected, double actual) {
  ASSERT_TRUE(std::abs(expected - actual) < 1e-9);
}

TEST(ConnectionFlowController, AntiChurnDelaysReconnectsUntilMinimumIntervalElapses) {
  auto params = default_runtime_stealth_params();
  params.flow_behavior.anti_churn_min_reconnect_interval_ms = 300;

  ConnectionFlowController controller;
  controller.on_connect_started(10.0, params.flow_behavior);

  assert_double_eq(10.3, controller.get_wakeup_at(10.05, params.flow_behavior));
  assert_double_eq(10.3, controller.get_wakeup_at(10.3, params.flow_behavior));
}

TEST(ConnectionFlowController, ConnectRateLimitBlocksBurstUntilWindowExpires) {
  auto params = default_runtime_stealth_params();
  params.flow_behavior.max_connects_per_10s_per_destination = 2;
  params.flow_behavior.anti_churn_min_reconnect_interval_ms = 50;

  ConnectionFlowController controller;
  controller.on_connect_started(0.0, params.flow_behavior);
  controller.on_connect_started(1.0, params.flow_behavior);

  assert_double_eq(10.0, controller.get_wakeup_at(5.0, params.flow_behavior));
  assert_double_eq(10.0, controller.get_wakeup_at(10.0, params.flow_behavior));
}

TEST(ConnectionFlowController, SlidingWindowPrunesExpiredAttemptsBeforeApplyingLimit) {
  auto params = default_runtime_stealth_params();
  params.flow_behavior.max_connects_per_10s_per_destination = 2;
  params.flow_behavior.anti_churn_min_reconnect_interval_ms = 50;

  ConnectionFlowController controller;
  controller.on_connect_started(0.0, params.flow_behavior);
  controller.on_connect_started(1.0, params.flow_behavior);

  assert_double_eq(10.0, controller.get_wakeup_at(9.9, params.flow_behavior));
  assert_double_eq(10.0, controller.get_wakeup_at(10.0, params.flow_behavior));

  controller.on_connect_started(10.0, params.flow_behavior);
  assert_double_eq(11.0, controller.get_wakeup_at(10.2, params.flow_behavior));
}

TEST(ConnectionFlowController, RateLimitAndAntiChurnUseTheStricterWakeup) {
  auto params = default_runtime_stealth_params();
  params.flow_behavior.max_connects_per_10s_per_destination = 3;
  params.flow_behavior.anti_churn_min_reconnect_interval_ms = 900;

  ConnectionFlowController controller;
  controller.on_connect_started(0.0, params.flow_behavior);
  controller.on_connect_started(0.3, params.flow_behavior);
  controller.on_connect_started(0.6, params.flow_behavior);

  assert_double_eq(10.0, controller.get_wakeup_at(0.7, params.flow_behavior));
}

TEST(ConnectionFlowController, OverLimitBurstRetainsFullWindowHistoryUntilBudgetRecovers) {
  auto params = default_runtime_stealth_params();
  params.flow_behavior.max_connects_per_10s_per_destination = 2;
  params.flow_behavior.anti_churn_min_reconnect_interval_ms = 50;

  ConnectionFlowController controller;
  controller.on_connect_started(0.0, params.flow_behavior);
  controller.on_connect_started(0.1, params.flow_behavior);
  controller.on_connect_started(0.2, params.flow_behavior);
  controller.on_connect_started(0.3, params.flow_behavior);

  assert_double_eq(10.2, controller.get_wakeup_at(0.4, params.flow_behavior));
  assert_double_eq(10.2, controller.get_wakeup_at(10.1, params.flow_behavior));
}

}  // namespace