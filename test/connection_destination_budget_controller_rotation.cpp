// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/ConnectionDestinationBudgetController.h"

#include "td/mtproto/stealth/StealthRuntimeParams.h"

#include "td/utils/tests.h"

namespace {

using td::ConnectionDestinationBudgetController;
using td::mtproto::stealth::default_runtime_stealth_params;

ConnectionDestinationBudgetController::DestinationKey make_destination(td::int32 dc_id, bool is_media = false,
                                                                       td::int32 proxy_id = 0,
                                                                       bool allow_media_only = false) {
  ConnectionDestinationBudgetController::DestinationKey destination;
  destination.dc_id = dc_id;
  destination.proxy_id = proxy_id;
  destination.allow_media_only = allow_media_only;
  destination.is_media = is_media;
  return destination;
}

TEST(ConnectionDestinationBudgetControllerRotation, AllowsOverlapWhenProjectedShareStaysWithinBudget) {
  auto params = default_runtime_stealth_params();
  params.flow_behavior.max_destination_share = 0.70;

  ConnectionDestinationBudgetController controller;
  auto destination_a = make_destination(2);
  auto destination_b = make_destination(4);

  controller.on_connect_started(0.0, destination_a, params.flow_behavior);
  controller.on_connect_started(1.0, destination_b, params.flow_behavior);

  ASSERT_TRUE(controller.allows_overlap_at(2.0, destination_a, params.flow_behavior));
}

TEST(ConnectionDestinationBudgetControllerRotation, BlocksOverlapWhenProjectedShareWouldExceedBudget) {
  auto params = default_runtime_stealth_params();
  params.flow_behavior.max_destination_share = 0.70;

  ConnectionDestinationBudgetController controller;
  auto destination_a = make_destination(2);
  auto destination_b = make_destination(4);

  controller.on_connect_started(0.0, destination_a, params.flow_behavior);
  controller.on_connect_started(1.0, destination_b, params.flow_behavior);
  controller.on_connect_started(2.0, destination_a, params.flow_behavior);

  ASSERT_FALSE(controller.allows_overlap_at(3.0, destination_a, params.flow_behavior));
  ASSERT_TRUE(controller.allows_overlap_at(3.0, destination_b, params.flow_behavior));
}

TEST(ConnectionDestinationBudgetControllerRotation, BlocksOverlapDuringDestinationSpecificAntiChurnWindow) {
  auto params = default_runtime_stealth_params();
  params.flow_behavior.anti_churn_min_reconnect_interval_ms = 300;

  ConnectionDestinationBudgetController controller;
  auto destination_a = make_destination(2);
  auto destination_b = make_destination(4);

  controller.on_connect_started(10.0, destination_a, params.flow_behavior);

  ASSERT_FALSE(controller.allows_overlap_at(10.05, destination_a, params.flow_behavior));
  ASSERT_TRUE(controller.allows_overlap_at(10.05, destination_b, params.flow_behavior));
}

}  // namespace