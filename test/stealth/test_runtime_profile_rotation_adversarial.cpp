// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Adversarial path: a hostile operator wants to poison the quarantine map across
// keys it should not share — empty destinations, case aliases, the ECH-on/off
// split, foreign platforms — or to drive rotation from wrong-secret noise, or to
// pass the iOS closure off as relabeled advisory evidence. None must succeed.

#include "RuntimeProfileRotationTestSupport.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using namespace td::mtproto::stealth;
using namespace td::mtproto::stealth::rotation_test;
using td::mtproto::BrowserProfile;

constexpr td::int32 kUnixTime = 1712345678;
constexpr td::uint32 kThreshold = 2;

// An empty destination has no stable quarantine identity: recording must be a
// no-op (no counter, no crash) and selection must not fan out an ambiguous key.
TEST(RuntimeProfileRotationAdversarial, EmptyDestinationNoCrashNoFanout) {
  RotationTestGuard guard;
  auto params = rotation_params(linux_platform(), TransportConfidence::Strong, false, true, kThreshold);
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  quarantine_variant("", BrowserProfile::Chrome133, /*hello_uses_ech=*/false, 8);
  ASSERT_EQ(0u, get_runtime_profile_rotation_counters().profile_failure_recorded_total);

  auto decision = pick_runtime_profile_adaptive("", kUnixTime, linux_platform(), EchMode::Disabled);
  ASSERT_FALSE(decision.avoided_quarantined_profile);
  ASSERT_EQ(0u, decision.quarantined_candidate_count);
}

// Case-only and trailing-dot aliases of one destination must share quarantine
// state, so an attacker cannot bypass a block by changing the casing.
TEST(RuntimeProfileRotationAdversarial, CaseAndDotAliasesShareQuarantine) {
  RotationTestGuard guard;
  auto params = rotation_params(linux_platform(), TransportConfidence::Strong, false, true, kThreshold);
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  auto baseline = pick_runtime_profile_adaptive("alias.example.com", kUnixTime, linux_platform(), EchMode::Disabled);
  // Record the failures under differently-cased / dotted spellings of the same host.
  quarantine_variant("Alias.Example.COM.", baseline.profile, baseline.hello_uses_ech, kThreshold);

  auto after = pick_runtime_profile_adaptive("alias.example.com", kUnixTime, linux_platform(), EchMode::Disabled);
  ASSERT_TRUE(after.avoided_quarantined_profile);
  ASSERT_TRUE(after.profile != baseline.profile);
}

// IOS14 and AppleIosTls currently emit the same wire image for the same inputs.
// Quarantining one must therefore quarantine the other too; otherwise rotation can
// claim it escaped a blocked fingerprint while only switching enum labels.
TEST(RuntimeProfileRotationAdversarial, WireIdenticalIosAliasesShareOneQuarantineUnit) {
  RotationTestGuard guard;
  auto params = rotation_params(ios_platform(), TransportConfidence::Unknown, false, true, kThreshold);
  params.profile_weights = {};
  params.profile_weights.ios14 = 1;
  params.profile_weights.apple_ios_tls = 1;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  const td::string dest = "ios-alias.example.com";
  auto baseline = pick_runtime_profile_adaptive(dest, kUnixTime, ios_platform(), EchMode::Disabled);
  ASSERT_TRUE(baseline.profile == BrowserProfile::IOS14 || baseline.profile == BrowserProfile::AppleIosTls);

  quarantine_variant(dest, baseline.profile, baseline.hello_uses_ech, kThreshold);
  auto blocked_before = get_runtime_profile_rotation_counters().profile_quarantine_all_blocked_total;

  auto after = pick_runtime_profile_adaptive(dest, kUnixTime, ios_platform(), EchMode::Disabled);
  ASSERT_FALSE(after.avoided_quarantined_profile);
  ASSERT_TRUE(after.profile == baseline.profile);
  ASSERT_TRUE(get_runtime_profile_rotation_counters().profile_quarantine_all_blocked_total > blocked_before);
}

// Quarantining the ECH-on wire variant must not poison the ECH-off variant of the
// same profile/destination: they are distinct wire shapes.
TEST(RuntimeProfileRotationAdversarial, EchSplitDoesNotPoisonOtherVariant) {
  RotationTestGuard guard;
  auto params = rotation_params(linux_platform(), TransportConfidence::Strong, false, true, kThreshold);
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());
  const td::string dest = "echsplit.example.com";

  auto with_ech = pick_runtime_profile_adaptive(dest, kUnixTime, linux_platform(), EchMode::Rfc9180Outer);
  ASSERT_TRUE(with_ech.hello_uses_ech);  // Linux Chrome/Firefox lanes permit ECH

  quarantine_variant(dest, with_ech.profile, /*hello_uses_ech=*/true, kThreshold);

  // The ECH-on variant is now quarantined and rotates.
  auto again_ech = pick_runtime_profile_adaptive(dest, kUnixTime, linux_platform(), EchMode::Rfc9180Outer);
  ASSERT_TRUE(again_ech.avoided_quarantined_profile);

  // The ECH-off variant of the same baseline is untouched: same baseline, no avoid.
  auto no_ech = pick_runtime_profile_adaptive(dest, kUnixTime, linux_platform(), EchMode::Disabled);
  ASSERT_FALSE(no_ech.avoided_quarantined_profile);
  ASSERT_TRUE(no_ech.profile == with_ech.profile);
}

// Quarantine state for a profile that is not in the current platform's allowed set
// has no effect: Android quarantine never leaks into a desktop platform.
TEST(RuntimeProfileRotationAdversarial, ForeignPlatformQuarantineHasNoEffect) {
  RotationTestGuard guard;
  auto params = rotation_params(linux_platform(), TransportConfidence::Strong, false, true, kThreshold);
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());
  const td::string dest = "isolation.example.com";

  quarantine_variant(dest, BrowserProfile::AndroidChromium_Alps, /*hello_uses_ech=*/false, kThreshold);
  quarantine_variant(dest, BrowserProfile::IOS14, /*hello_uses_ech=*/false, kThreshold);

  auto decision = pick_runtime_profile_adaptive(dest, kUnixTime, linux_platform(), EchMode::Disabled);
  ASSERT_FALSE(decision.avoided_quarantined_profile);
  ASSERT_EQ(0u, decision.quarantined_candidate_count);
  ASSERT_TRUE(platform_allows(linux_platform(), decision.profile));
}

// Repeated wrong-secret noise must never rotate the profile (false-positive
// resistance): the baseline stays sticky regardless of how many hash mismatches
// are reported.
TEST(RuntimeProfileRotationAdversarial, RepeatedHashMismatchNeverRotates) {
  RotationTestGuard guard;
  auto params = rotation_params(linux_platform(), TransportConfidence::Strong, false, true, kThreshold);
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());
  const td::string dest = "noise.example.com";

  auto baseline = pick_runtime_profile_adaptive(dest, kUnixTime, linux_platform(), EchMode::Disabled);
  for (int i = 0; i < 64; i++) {
    note_runtime_profile_failure(dest, RuntimeProfileWireVariant{baseline.profile, baseline.hello_uses_ech},
                                 RuntimeProfileFailureSignal::ResponseHashMismatch);
  }
  auto after = pick_runtime_profile_adaptive(dest, kUnixTime, linux_platform(), EchMode::Disabled);
  ASSERT_TRUE(after.profile == baseline.profile);
  ASSERT_FALSE(after.avoided_quarantined_profile);
}

// The iOS/default closure must be real evidence, never advisory IOS14 relabeled as
// release-grade: AppleIosTls carries verified browser-capture provenance while
// IOS14 stays advisory utls and non-release.
TEST(RuntimeProfileRotationAdversarial, IosClosureNotRelabeledAdvisory) {
  RotationTestGuard guard;
  const auto &apple = profile_fixture_metadata(BrowserProfile::AppleIosTls);
  const auto &ios14 = profile_fixture_metadata(BrowserProfile::IOS14);

  ASSERT_TRUE(apple.source_kind == ProfileFixtureSourceKind::BrowserCapture);
  ASSERT_TRUE(apple.trust_tier == ProfileTrustTier::Verified);
  ASSERT_TRUE(apple.release_gating);
  ASSERT_TRUE(apple.transport_claim_level == TransportClaimLevel::TlsOnly);

  ASSERT_TRUE(ios14.source_kind == ProfileFixtureSourceKind::UtlsSnapshot);
  ASSERT_TRUE(ios14.trust_tier == ProfileTrustTier::Advisory);
  ASSERT_FALSE(ios14.release_gating);

  // They are genuinely distinct lanes, not the same metadata under two names.
  ASSERT_TRUE(BrowserProfile::AppleIosTls != BrowserProfile::IOS14);
}

}  // namespace
