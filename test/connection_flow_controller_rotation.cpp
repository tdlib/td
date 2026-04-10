// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/ConnectionFlowController.h"

#include "td/mtproto/stealth/StealthRuntimeParams.h"

#include "td/utils/tests.h"

namespace {

using td::ConnectionFlowController;
using td::mtproto::stealth::default_runtime_stealth_params;

TEST(ConnectionFlowControllerRotation, AllowsRotationWhenControllerWakeupHasElapsed) {
  auto params = default_runtime_stealth_params();
  params.flow_behavior.anti_churn_min_reconnect_interval_ms = 300;

  ConnectionFlowController controller;
  controller.on_connect_started(10.0, params.flow_behavior);

  ASSERT_TRUE(controller.allows_rotation_at(10.3, params.flow_behavior));
}

TEST(ConnectionFlowControllerRotation, BlocksRotationBeforeAntiChurnWindowElapses) {
  auto params = default_runtime_stealth_params();
  params.flow_behavior.anti_churn_min_reconnect_interval_ms = 300;

  ConnectionFlowController controller;
  controller.on_connect_started(10.0, params.flow_behavior);

  ASSERT_FALSE(controller.allows_rotation_at(10.299, params.flow_behavior));
}

TEST(ConnectionFlowControllerRotation, BlocksRotationWhileBurstLimitWindowIsStillActive) {
  auto params = default_runtime_stealth_params();
  params.flow_behavior.max_connects_per_10s_per_destination = 2;
  params.flow_behavior.anti_churn_min_reconnect_interval_ms = 50;

  ConnectionFlowController controller;
  controller.on_connect_started(0.0, params.flow_behavior);
  controller.on_connect_started(1.0, params.flow_behavior);

  ASSERT_FALSE(controller.allows_rotation_at(9.999, params.flow_behavior));
  ASSERT_TRUE(controller.allows_rotation_at(10.0, params.flow_behavior));
}

}  // namespace