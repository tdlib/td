//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::DesktopOs;
using td::mtproto::stealth::DeviceClass;
using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::get_runtime_ech_decision;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::pick_runtime_profile;
using td::mtproto::stealth::ProfileWeights;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
using td::mtproto::stealth::reset_runtime_stealth_params_for_tests;
using td::mtproto::stealth::RuntimePlatformHints;
using td::mtproto::stealth::set_runtime_stealth_params_for_tests;
using td::mtproto::stealth::StealthRuntimeParams;
using td::mtproto::test::MockRng;

class RuntimeParamsGuard final {
 public:
  RuntimeParamsGuard() {
    reset_runtime_stealth_params_for_tests();
    reset_runtime_ech_failure_state_for_tests();
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

TEST(TlsRuntimeParams, ProfileWeightsOverrideRuntimeSelection) {
  RuntimeParamsGuard guard;
  MockRng rng(1);

  StealthRuntimeParams params;
  params.profile_weights = ProfileWeights{};
  params.profile_weights.chrome133 = 0;
  params.profile_weights.chrome131 = 0;
  params.profile_weights.chrome120 = 0;
  params.profile_weights.firefox148 = 100;
  params.profile_weights.safari26_3 = 0;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  auto platform = make_linux_platform();
  for (td::int32 day = 0; day < 64; day++) {
    auto unix_time = 1712345678 + day * 86400;
    auto profile = pick_runtime_profile("runtime-policy.example.com", unix_time, platform);
    ASSERT_TRUE(profile == BrowserProfile::Firefox148);
  }
}

TEST(TlsRuntimeParams, RoutePolicyOverrideCanDisableHealthyNonRuEch) {
  RuntimeParamsGuard guard;

  StealthRuntimeParams params;
  params.route_policy.non_ru.ech_mode = EchMode::Disabled;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  auto decision = get_runtime_ech_decision("runtime-policy.example.com", 1712345678, known_non_ru_route());
  ASSERT_TRUE(decision.ech_mode == EchMode::Disabled);
  ASSERT_TRUE(decision.disabled_by_route);
  ASSERT_FALSE(decision.disabled_by_circuit_breaker);
}

TEST(TlsRuntimeParams, InvalidRouteOverrideIsRejectedWithoutReplacingLastKnownGoodSnapshot) {
  RuntimeParamsGuard guard;

  StealthRuntimeParams params;
  params.route_policy.non_ru.ech_mode = EchMode::Disabled;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  auto good_decision = get_runtime_ech_decision("runtime-policy.example.com", 1712345678, known_non_ru_route());
  ASSERT_TRUE(good_decision.ech_mode == EchMode::Disabled);
  ASSERT_TRUE(good_decision.disabled_by_route);

  StealthRuntimeParams invalid_params = params;
  invalid_params.route_policy.unknown.ech_mode = EchMode::Rfc9180Outer;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(invalid_params).is_error());

  auto rejected_decision = get_runtime_ech_decision("runtime-policy.example.com", 1712345678, known_non_ru_route());
  ASSERT_TRUE(rejected_decision.ech_mode == EchMode::Disabled);
  ASSERT_TRUE(rejected_decision.disabled_by_route);
}

TEST(TlsRuntimeParams, CircuitBreakerThresholdOverrideCanTightenBudget) {
  RuntimeParamsGuard guard;

  StealthRuntimeParams params;
  params.route_failure.ech_failure_threshold = 1;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  td::mtproto::stealth::note_runtime_ech_failure("runtime-policy.example.com", 1712345678);
  auto decision = get_runtime_ech_decision("runtime-policy.example.com", 1712345678, known_non_ru_route());
  ASSERT_TRUE(decision.ech_mode == EchMode::Disabled);
  ASSERT_TRUE(decision.disabled_by_circuit_breaker);
}

TEST(TlsRuntimeParams, CircuitBreakerThresholdOverrideCanResistPrematureDisable) {
  RuntimeParamsGuard guard;

  StealthRuntimeParams params;
  params.route_failure.ech_failure_threshold = 4;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  td::mtproto::stealth::note_runtime_ech_failure("runtime-policy.example.com", 1712345678);
  td::mtproto::stealth::note_runtime_ech_failure("runtime-policy.example.com", 1712345678);
  td::mtproto::stealth::note_runtime_ech_failure("runtime-policy.example.com", 1712345678);

  auto decision = get_runtime_ech_decision("runtime-policy.example.com", 1712345678, known_non_ru_route());
  ASSERT_TRUE(decision.ech_mode == EchMode::Rfc9180Outer);
  ASSERT_FALSE(decision.disabled_by_circuit_breaker);
}

}  // namespace