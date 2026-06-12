// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Positive path: with rotation enabled, quarantining the selected wire variant
// for a destination lets the next attempt rotate to another already-allowed,
// non-quarantined lane — on every platform that has at least two selectable
// lanes — and the verified Apple iOS TLS lane is the reachable iOS release lane.

#include "RuntimeProfileRotationTestSupport.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using namespace td::mtproto::stealth;
using namespace td::mtproto::stealth::rotation_test;
using td::mtproto::BrowserProfile;

constexpr td::int32 kUnixTime = 1712345678;
constexpr td::uint32 kThreshold = 2;

// Drives one rotation: finds the baseline for `destination`, quarantines its wire
// variant, and asserts the next selection avoids it but stays in the allowed set.
void assert_rotates_within_allowed(const RuntimePlatformHints &platform, TransportConfidence confidence,
                                   td::Slice destination) {
  auto params = rotation_params(platform, confidence, /*release_gating=*/false, /*rotation_enabled=*/true, kThreshold);
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  auto baseline = pick_runtime_profile_adaptive(destination, kUnixTime, platform, EchMode::Disabled);
  ASSERT_FALSE(baseline.avoided_quarantined_profile);

  quarantine_variant(destination, baseline.profile, baseline.hello_uses_ech, kThreshold);

  auto rotated = pick_runtime_profile_adaptive(destination, kUnixTime, platform, EchMode::Disabled);
  ASSERT_TRUE(rotated.avoided_quarantined_profile);
  ASSERT_TRUE(rotated.profile != baseline.profile);
  ASSERT_TRUE(platform_allows(platform, rotated.profile));
  ASSERT_TRUE(rotated.quarantined_candidate_count >= 1);
}

TEST(RuntimeProfileRotationPositive, WindowsRotatesChromeToFirefox) {
  RotationTestGuard guard;
  assert_rotates_within_allowed(windows_platform(), TransportConfidence::Strong, "win-rotate.example.com");
}

TEST(RuntimeProfileRotationPositive, LinuxRotatesToAnotherLinuxLane) {
  RotationTestGuard guard;
  assert_rotates_within_allowed(linux_platform(), TransportConfidence::Strong, "linux-rotate.example.com");
}

TEST(RuntimeProfileRotationPositive, DarwinRotatesToAnotherDarwinLane) {
  RotationTestGuard guard;
  assert_rotates_within_allowed(darwin_platform(), TransportConfidence::Strong, "darwin-rotate.example.com");
}

TEST(RuntimeProfileRotationPositive, AndroidStrongRotatesAcrossVerifiedLanes) {
  RotationTestGuard guard;
  assert_rotates_within_allowed(android_platform(), TransportConfidence::Strong, "android-rotate.example.com");
}

// The verified Apple iOS TLS lane is the reachable iOS lane under release-mode
// gating at Unknown confidence, and release selection never falls back to the
// advisory IOS14 lane.
TEST(RuntimeProfileRotationPositive, IosUnknownReleaseReachesAppleIosTlsNeverAdvisory) {
  RotationTestGuard guard;
  auto params = rotation_params(ios_platform(), TransportConfidence::Unknown, /*release_gating=*/true,
                                /*rotation_enabled=*/false);
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  bool saw_apple_ios_tls = false;
  for (int i = 0; i < 96; i++) {
    auto decision = pick_runtime_profile_adaptive("ios-rel-" + td::to_string(i) + ".example", kUnixTime + i,
                                                  ios_platform(), EchMode::Disabled);
    ASSERT_TRUE(decision.profile == BrowserProfile::AppleIosTls);
    ASSERT_TRUE(decision.profile != BrowserProfile::IOS14);
    saw_apple_ios_tls = true;
  }
  ASSERT_TRUE(saw_apple_ios_tls);
}

// A verified hello-success clears the quarantine for that exact wire variant, so
// the destination returns to its sticky baseline.
TEST(RuntimeProfileRotationPositive, SuccessClearsQuarantineAndRestoresBaseline) {
  RotationTestGuard guard;
  auto params = rotation_params(linux_platform(), TransportConfidence::Strong, false, true, kThreshold);
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());
  const td::string dest = "linux-clear.example.com";

  auto baseline = pick_runtime_profile_adaptive(dest, kUnixTime, linux_platform(), EchMode::Disabled);
  quarantine_variant(dest, baseline.profile, baseline.hello_uses_ech, kThreshold);
  ASSERT_TRUE(pick_runtime_profile_adaptive(dest, kUnixTime, linux_platform(), EchMode::Disabled)
                  .avoided_quarantined_profile);

  note_runtime_profile_success(dest, RuntimeProfileWireVariant{baseline.profile, baseline.hello_uses_ech});

  auto restored = pick_runtime_profile_adaptive(dest, kUnixTime, linux_platform(), EchMode::Disabled);
  ASSERT_FALSE(restored.avoided_quarantined_profile);
  ASSERT_TRUE(restored.profile == baseline.profile);
}

}  // namespace
