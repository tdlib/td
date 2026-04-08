//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/net/ConnectionDestinationBudgetController.h"

#include "td/mtproto/stealth/StealthRuntimeParams.h"

#include "td/utils/tests.h"

#include <cmath>

namespace {

using td::ConnectionDestinationBudgetController;
using td::mtproto::stealth::default_runtime_stealth_params;

void assert_double_eq(double expected, double actual) {
  ASSERT_TRUE(std::abs(expected - actual) < 1e-9);
}

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

TEST(ConnectionDestinationBudgetController, AllowsBootstrapWhenOnlyOneDestinationExists) {
  auto params = default_runtime_stealth_params();
  params.flow_behavior.max_destination_share = 0.55;

  ConnectionDestinationBudgetController controller;
  auto destination = make_destination(2);
  controller.on_connect_started(0.0, destination, params.flow_behavior);
  controller.on_connect_started(1.0, destination, params.flow_behavior);

  assert_double_eq(2.0, controller.get_wakeup_at(2.0, destination, params.flow_behavior));
}

TEST(ConnectionDestinationBudgetController, BlocksDestinationThatWouldExceedShareBudget) {
  auto params = default_runtime_stealth_params();
  params.flow_behavior.max_destination_share = 0.70;

  ConnectionDestinationBudgetController controller;
  auto destination_a = make_destination(2);
  auto destination_b = make_destination(4);

  controller.on_connect_started(0.0, destination_a, params.flow_behavior);
  controller.on_connect_started(1.0, destination_b, params.flow_behavior);
  controller.on_connect_started(2.0, destination_a, params.flow_behavior);

  assert_double_eq(10.0, controller.get_wakeup_at(3.0, destination_a, params.flow_behavior));
  assert_double_eq(3.0, controller.get_wakeup_at(3.0, destination_b, params.flow_behavior));
}

TEST(ConnectionDestinationBudgetController, PrunesExpiredAttemptsBeforeEvaluatingShareBudget) {
  auto params = default_runtime_stealth_params();
  params.flow_behavior.max_destination_share = 0.60;

  ConnectionDestinationBudgetController controller;
  auto destination_a = make_destination(2);
  auto destination_b = make_destination(4);

  controller.on_connect_started(0.0, destination_a, params.flow_behavior);
  controller.on_connect_started(1.0, destination_b, params.flow_behavior);
  controller.on_connect_started(2.0, destination_a, params.flow_behavior);

  assert_double_eq(10.0, controller.get_wakeup_at(3.0, destination_a, params.flow_behavior));
  assert_double_eq(11.0, controller.get_wakeup_at(10.5, destination_b, params.flow_behavior));
  assert_double_eq(12.0, controller.get_wakeup_at(10.5, destination_a, params.flow_behavior));
  assert_double_eq(11.0, controller.get_wakeup_at(11.0, destination_a, params.flow_behavior));
}

TEST(ConnectionDestinationBudgetController, AntiChurnDelaysReconnectsForSameDestinationOnly) {
  auto params = default_runtime_stealth_params();
  params.flow_behavior.anti_churn_min_reconnect_interval_ms = 300;

  ConnectionDestinationBudgetController controller;
  auto destination_a = make_destination(2);
  auto destination_b = make_destination(4);

  controller.on_connect_started(10.0, destination_a, params.flow_behavior);

  assert_double_eq(10.3, controller.get_wakeup_at(10.05, destination_a, params.flow_behavior));
  assert_double_eq(10.05, controller.get_wakeup_at(10.05, destination_b, params.flow_behavior));
}

TEST(ConnectionDestinationBudgetController, ConnectRateLimitBlocksBurstForSameDestinationOnly) {
  auto params = default_runtime_stealth_params();
  params.flow_behavior.max_connects_per_10s_per_destination = 2;
  params.flow_behavior.anti_churn_min_reconnect_interval_ms = 50;

  ConnectionDestinationBudgetController controller;
  auto destination_a = make_destination(2);
  auto destination_b = make_destination(4);

  controller.on_connect_started(0.0, destination_a, params.flow_behavior);
  controller.on_connect_started(1.0, destination_a, params.flow_behavior);

  assert_double_eq(10.0, controller.get_wakeup_at(5.0, destination_a, params.flow_behavior));
  assert_double_eq(5.0, controller.get_wakeup_at(5.0, destination_b, params.flow_behavior));
}

TEST(ConnectionDestinationBudgetController, DestinationBudgetUsesFullRouteKeyIsolation) {
  auto params = default_runtime_stealth_params();
  params.flow_behavior.max_connects_per_10s_per_destination = 2;
  params.flow_behavior.anti_churn_min_reconnect_interval_ms = 300;

  ConnectionDestinationBudgetController controller;
  auto base_destination = make_destination(2, false, 1, false);
  auto proxy_variant = make_destination(2, false, 2, false);
  auto media_variant = make_destination(2, true, 1, false);
  auto media_only_variant = make_destination(2, false, 1, true);

  controller.on_connect_started(10.0, base_destination, params.flow_behavior);
  controller.on_connect_started(10.1, base_destination, params.flow_behavior);

  assert_double_eq(20.0, controller.get_wakeup_at(10.2, base_destination, params.flow_behavior));
  assert_double_eq(10.2, controller.get_wakeup_at(10.2, proxy_variant, params.flow_behavior));
  assert_double_eq(10.2, controller.get_wakeup_at(10.2, media_variant, params.flow_behavior));
  assert_double_eq(10.2, controller.get_wakeup_at(10.2, media_only_variant, params.flow_behavior));
}

TEST(ConnectionDestinationBudgetController, AntiChurnRemainsEnforcedBeyondShareWindow) {
  auto params = default_runtime_stealth_params();
  params.flow_behavior.anti_churn_min_reconnect_interval_ms = 15000;

  ConnectionDestinationBudgetController controller;
  auto destination = make_destination(2);

  controller.on_connect_started(0.0, destination, params.flow_behavior);

  assert_double_eq(15.0, controller.get_wakeup_at(10.5, destination, params.flow_behavior));
  assert_double_eq(15.0, controller.get_wakeup_at(15.0, destination, params.flow_behavior));
}

TEST(ConnectionDestinationBudgetController, PrunesExpiredDestinationStateAfterOutOfOrderRefresh) {
  auto params = default_runtime_stealth_params();
  params.flow_behavior.anti_churn_min_reconnect_interval_ms = 15000;

  ConnectionDestinationBudgetController controller;
  auto destination_a = make_destination(2);
  auto destination_b = make_destination(4);

  controller.on_connect_started(0.0, destination_a, params.flow_behavior);
  controller.on_connect_started(1.0, destination_b, params.flow_behavior);
  controller.on_connect_started(14.0, destination_a, params.flow_behavior);

  assert_double_eq(16.5, controller.get_wakeup_at(16.5, destination_b, params.flow_behavior));
  assert_double_eq(29.0, controller.get_wakeup_at(16.5, destination_a, params.flow_behavior));
}

}  // namespace