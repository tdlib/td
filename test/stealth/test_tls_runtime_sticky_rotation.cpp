// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::default_runtime_stealth_params;
using td::mtproto::stealth::DesktopOs;
using td::mtproto::stealth::DeviceClass;
using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::get_runtime_ech_decision;
using td::mtproto::stealth::make_profile_selection_key;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::note_runtime_ech_failure;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
using td::mtproto::stealth::reset_runtime_stealth_params_for_tests;
using td::mtproto::stealth::RuntimePlatformHints;
using td::mtproto::stealth::set_runtime_stealth_params_for_tests;

class RuntimeParamsGuard final {
 public:
  RuntimeParamsGuard() {
    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_stealth_params_for_tests();
  }

  ~RuntimeParamsGuard() {
    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_stealth_params_for_tests();
  }
};

RuntimePlatformHints make_linux_platform() {
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Desktop;
  platform.desktop_os = DesktopOs::Linux;
  return platform;
}

NetworkRouteHints known_non_ru_route() {
  NetworkRouteHints route;
  route.is_known = true;
  route.is_ru = false;
  return route;
}

TEST(TlsRuntimeStickyRotation, SelectionKeyUsesConfiguredStickyRotationWindow) {
  RuntimeParamsGuard guard;

  auto params = default_runtime_stealth_params();
  params.platform_hints = make_linux_platform();
  params.flow_behavior.sticky_domain_rotation_window_sec = 60;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  auto first = make_profile_selection_key("sticky-window.example.com", 59);
  auto second = make_profile_selection_key("sticky-window.example.com", 60);
  auto third = make_profile_selection_key("sticky-window.example.com", 119);

  ASSERT_EQ(0u, first.time_bucket);
  ASSERT_EQ(1u, second.time_bucket);
  ASSERT_EQ(second.time_bucket, third.time_bucket);
}

TEST(TlsRuntimeStickyRotation, EchCircuitBreakerStateSurvivesStickyRotationBoundary) {
  RuntimeParamsGuard guard;

  auto params = default_runtime_stealth_params();
  params.platform_hints = make_linux_platform();
  params.flow_behavior.sticky_domain_rotation_window_sec = 60;
  params.route_failure.ech_failure_threshold = 1;
  params.route_failure.ech_disable_ttl_seconds = 300.0;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  note_runtime_ech_failure("sticky-window.example.com", 0);

  auto decision = get_runtime_ech_decision("sticky-window.example.com", 61, known_non_ru_route());
  ASSERT_TRUE(decision.ech_mode == EchMode::Disabled);
  ASSERT_TRUE(decision.disabled_by_circuit_breaker);
}

}  // namespace