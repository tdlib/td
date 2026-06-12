// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Edge cases for the rotation policy bounds and the quarantine fallback set: the
// failure threshold and TTL are validated exactly at their documented limits, the
// missing policy preserves the disabled defaults, and zero-weighted lanes never
// become rotation fallbacks.

#include "RuntimeProfileRotationTestSupport.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using namespace td::mtproto::stealth;
using namespace td::mtproto::stealth::rotation_test;
using td::mtproto::BrowserProfile;

constexpr td::int32 kUnixTime = 1712345678;

StealthRuntimeParams base_params_with_rotation(td::uint32 threshold, double ttl_seconds) {
  auto params = default_runtime_stealth_params();
  params.profile_rotation.enabled = true;
  params.profile_rotation.failure_threshold = threshold;
  params.profile_rotation.quarantine_ttl_seconds = ttl_seconds;
  return params;
}

TEST(RuntimeProfileRotationEdge, FailureThresholdMinAndMaxAccepted) {
  RotationTestGuard guard;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(base_params_with_rotation(2, 300.0)).is_ok());
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(base_params_with_rotation(8, 300.0)).is_ok());
}

TEST(RuntimeProfileRotationEdge, FailureThresholdBelowMinRejected) {
  RotationTestGuard guard;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(base_params_with_rotation(1, 300.0)).is_error());
}

TEST(RuntimeProfileRotationEdge, FailureThresholdAboveMaxRejected) {
  RotationTestGuard guard;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(base_params_with_rotation(9, 300.0)).is_error());
}

TEST(RuntimeProfileRotationEdge, QuarantineTtlMinAndMaxAccepted) {
  RotationTestGuard guard;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(base_params_with_rotation(2, 30.0)).is_ok());
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(base_params_with_rotation(2, 3600.0)).is_ok());
}

TEST(RuntimeProfileRotationEdge, QuarantineTtlBelowMinRejected) {
  RotationTestGuard guard;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(base_params_with_rotation(2, 29.999)).is_error());
}

TEST(RuntimeProfileRotationEdge, QuarantineTtlAboveMaxRejected) {
  RotationTestGuard guard;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(base_params_with_rotation(2, 3600.001)).is_error());
}

TEST(RuntimeProfileRotationEdge, MissingPolicyPreservesDisabledDefaults) {
  RotationTestGuard guard;
  auto policy = default_runtime_stealth_params().profile_rotation;
  ASSERT_FALSE(policy.enabled);
  ASSERT_EQ(2u, policy.failure_threshold);
  ASSERT_TRUE(policy.quarantine_ttl_seconds == 300.0);
}

// A non-baseline lane pinned to weight 0 is not selectable, so it must never
// become a rotation fallback when the only weighted lane is quarantined: the
// selector fails closed instead of widening onto the zero-weight lane.
TEST(RuntimeProfileRotationEdge, ZeroWeightedAlternativeIsNotAFallback) {
  RotationTestGuard guard;
  auto params = rotation_params(windows_platform(), TransportConfidence::Strong, false, true, 2);
  params.profile_weights.firefox149_windows = 0;  // leave only Chrome147_Windows selectable
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());
  const td::string dest = "win-zeroweight.example.com";

  auto baseline = pick_runtime_profile_adaptive(dest, kUnixTime, windows_platform(), EchMode::Disabled);
  ASSERT_TRUE(baseline.profile == BrowserProfile::Chrome147_Windows);

  quarantine_variant(dest, baseline.profile, baseline.hello_uses_ech, 2);
  auto after = pick_runtime_profile_adaptive(dest, kUnixTime, windows_platform(), EchMode::Disabled);
  ASSERT_FALSE(after.avoided_quarantined_profile);
  ASSERT_TRUE(after.profile == BrowserProfile::Chrome147_Windows);
  ASSERT_TRUE(after.profile != BrowserProfile::Firefox149_Windows);
}

}  // namespace
