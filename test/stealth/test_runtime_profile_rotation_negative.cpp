// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Negative path: rotation must never widen beyond the already-allowed,
// confidence/release-eligible set, never quarantine on wrong-secret/wrong-regime
// signals, and must fail closed (incrementing the all-blocked counter) when every
// eligible candidate is quarantined.

#include "RuntimeProfileRotationTestSupport.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using namespace td::mtproto::stealth;
using namespace td::mtproto::stealth::rotation_test;
using td::mtproto::BrowserProfile;

constexpr td::int32 kUnixTime = 1712345678;
constexpr td::uint32 kThreshold = 2;

// At Unknown confidence Android exposes only the advisory TlsOnly lane; the
// cross-layer-claim lanes are not selectable. Quarantining the advisory lane must
// not unlock them — the selector stays on it and fails closed.
TEST(RuntimeProfileRotationNegative, UnknownAndroidQuarantineDoesNotUnlockCrossLayerLanes) {
  RotationTestGuard guard;
  auto params = rotation_params(android_platform(), TransportConfidence::Unknown, false, true, kThreshold);
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());
  const td::string dest = "android-unk.example.com";

  auto baseline = pick_runtime_profile_adaptive(dest, kUnixTime, android_platform(), EchMode::Disabled);
  ASSERT_TRUE(baseline.profile == BrowserProfile::Android11_OkHttp_Advisory);

  quarantine_variant(dest, baseline.profile, baseline.hello_uses_ech, kThreshold);
  auto blocked_before = get_runtime_profile_rotation_counters().profile_quarantine_all_blocked_total;

  auto after = pick_runtime_profile_adaptive(dest, kUnixTime, android_platform(), EchMode::Disabled);
  ASSERT_TRUE(after.profile == BrowserProfile::Android11_OkHttp_Advisory);
  ASSERT_FALSE(after.avoided_quarantined_profile);
  ASSERT_TRUE(after.profile != BrowserProfile::AndroidChromium_Alps);
  ASSERT_TRUE(after.profile != BrowserProfile::Firefox149_Android);
  ASSERT_TRUE(get_runtime_profile_rotation_counters().profile_quarantine_all_blocked_total > blocked_before);
}

// Under release-mode gating the only release-eligible Android lane is the verified
// Chromium ALPS lane; quarantining it must not unlock the non-release advisory or
// firefox lanes — the selector stays fail-closed on the release lane.
TEST(RuntimeProfileRotationNegative, ReleaseAndroidQuarantineStaysFailClosed) {
  RotationTestGuard guard;
  auto params = rotation_params(android_platform(), TransportConfidence::Strong, /*release_gating=*/true, true,
                                kThreshold);
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());
  const td::string dest = "android-rel.example.com";

  auto baseline = pick_runtime_profile_adaptive(dest, kUnixTime, android_platform(), EchMode::Disabled);
  ASSERT_TRUE(baseline.profile == BrowserProfile::AndroidChromium_Alps);

  quarantine_variant(dest, baseline.profile, baseline.hello_uses_ech, kThreshold);
  auto blocked_before = get_runtime_profile_rotation_counters().profile_quarantine_all_blocked_total;

  auto after = pick_runtime_profile_adaptive(dest, kUnixTime, android_platform(), EchMode::Disabled);
  ASSERT_TRUE(after.profile == BrowserProfile::AndroidChromium_Alps);
  ASSERT_FALSE(after.avoided_quarantined_profile);
  ASSERT_TRUE(after.profile != BrowserProfile::Firefox149_Android);
  ASSERT_TRUE(after.profile != BrowserProfile::Android11_OkHttp_Advisory);
  ASSERT_TRUE(get_runtime_profile_rotation_counters().profile_quarantine_all_blocked_total > blocked_before);
}

// Response-hash mismatch points at a wrong proxy secret / TLS-init contract, not a
// blocked fingerprint: it must never quarantine, never rotate, never bump the
// failure-recorded counter.
TEST(RuntimeProfileRotationNegative, ResponseHashMismatchNeverQuarantines) {
  RotationTestGuard guard;
  auto params = rotation_params(linux_platform(), TransportConfidence::Strong, false, true, kThreshold);
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());
  const td::string dest = "linux-hash.example.com";

  auto baseline = pick_runtime_profile_adaptive(dest, kUnixTime, linux_platform(), EchMode::Disabled);
  for (int i = 0; i < 8; i++) {
    note_runtime_profile_failure(dest, RuntimeProfileWireVariant{baseline.profile, baseline.hello_uses_ech},
                                 RuntimeProfileFailureSignal::ResponseHashMismatch);
  }
  auto after = pick_runtime_profile_adaptive(dest, kUnixTime, linux_platform(), EchMode::Disabled);
  ASSERT_TRUE(after.profile == baseline.profile);
  ASSERT_FALSE(after.avoided_quarantined_profile);
  ASSERT_EQ(0u, after.quarantined_candidate_count);
  ASSERT_EQ(0u, get_runtime_profile_rotation_counters().profile_failure_recorded_total);
}

// Wrong-regime rejection points at a protocol mismatch no fingerprint can repair.
TEST(RuntimeProfileRotationNegative, WrongRegimeNeverQuarantines) {
  RotationTestGuard guard;
  auto params = rotation_params(linux_platform(), TransportConfidence::Strong, false, true, kThreshold);
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());
  const td::string dest = "linux-regime.example.com";

  auto baseline = pick_runtime_profile_adaptive(dest, kUnixTime, linux_platform(), EchMode::Disabled);
  for (int i = 0; i < 8; i++) {
    note_runtime_profile_failure(dest, RuntimeProfileWireVariant{baseline.profile, baseline.hello_uses_ech},
                                 RuntimeProfileFailureSignal::WrongRegime);
  }
  auto after = pick_runtime_profile_adaptive(dest, kUnixTime, linux_platform(), EchMode::Disabled);
  ASSERT_TRUE(after.profile == baseline.profile);
  ASSERT_EQ(0u, get_runtime_profile_rotation_counters().profile_failure_recorded_total);
}

// When every eligible candidate is quarantined the selector stays inside the
// already-allowed platform set (keeps the baseline) and increments the all-blocked
// counter instead of widening.
TEST(RuntimeProfileRotationNegative, AllCandidatesQuarantinedStaysInAllowedAndCountsBlocked) {
  RotationTestGuard guard;
  auto params = rotation_params(linux_platform(), TransportConfidence::Strong, false, true, kThreshold);
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());
  const td::string dest = "linux-allblocked.example.com";

  for (auto profile : allowed_profiles_for_platform(linux_platform())) {
    quarantine_variant(dest, profile, /*hello_uses_ech=*/false, kThreshold);
  }
  auto blocked_before = get_runtime_profile_rotation_counters().profile_quarantine_all_blocked_total;

  auto after = pick_runtime_profile_adaptive(dest, kUnixTime, linux_platform(), EchMode::Disabled);
  ASSERT_FALSE(after.avoided_quarantined_profile);
  ASSERT_TRUE(platform_allows(linux_platform(), after.profile));
  ASSERT_TRUE(get_runtime_profile_rotation_counters().profile_quarantine_all_blocked_total > blocked_before);
}

}  // namespace
