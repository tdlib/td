// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Contract pins for adaptive runtime profile rotation. These anchor the stable
// public behaviour the rest of the suite (positive/negative/adversarial) builds
// on: the disabled path stays the legacy weighted baseline, adaptive selection
// never escapes the platform allowed set, the decision reports its exact wire
// variant, and the iOS/default release-grade closure is real (AppleIosTls is the
// verified TlsOnly + release-gated iOS lane, not relabeled IOS14 advisory).

#include "td/mtproto/BrowserProfile.h"
#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/Span.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::BrowserProfile;
using namespace td::mtproto::stealth;

constexpr td::int32 kUnixTime = 1712345678;

class Guard final {
 public:
  Guard() {
    reset();
  }
  ~Guard() {
    reset();
  }

 private:
  static void reset() {
    reset_runtime_stealth_params_for_tests();
    reset_runtime_profile_quarantine_state_for_tests();
    reset_runtime_profile_rotation_counters_for_tests();
    reset_per_install_selection_salt_for_tests();
  }
};

RuntimePlatformHints ios_platform() {
  return RuntimePlatformHints{DeviceClass::Mobile, MobileOs::IOS, DesktopOs::Unknown};
}
RuntimePlatformHints linux_platform() {
  return RuntimePlatformHints{DeviceClass::Desktop, MobileOs::None, DesktopOs::Linux};
}
RuntimePlatformHints windows_platform() {
  return RuntimePlatformHints{DeviceClass::Desktop, MobileOs::None, DesktopOs::Windows};
}
RuntimePlatformHints darwin_platform() {
  return RuntimePlatformHints{DeviceClass::Desktop, MobileOs::None, DesktopOs::Darwin};
}

bool platform_allows(const RuntimePlatformHints &platform, BrowserProfile profile) {
  for (auto allowed : allowed_profiles_for_platform(platform)) {
    if (allowed == profile) {
      return true;
    }
  }
  return false;
}

StealthRuntimeParams params_for(const RuntimePlatformHints &platform, TransportConfidence confidence,
                                bool release_gating, bool rotation_enabled) {
  auto params = default_runtime_stealth_params();
  params.platform_hints = platform;
  // Struct-default weights keep every platform lane non-zero, so the params stay
  // valid across the platform override (validation only requires a positive
  // allowed/TlsOnly/release weight, not a policy sum).
  params.profile_weights = ProfileWeights{};
  params.transport_confidence = confidence;
  params.release_mode_profile_gating = release_gating;
  params.profile_rotation.enabled = rotation_enabled;
  return params;
}

// 1. Legacy pick_runtime_profile stays deterministic, and the adaptive path with
//    rotation disabled returns exactly that baseline.
TEST(RuntimeProfileRotationContract, DisabledAdaptiveEqualsLegacyBaseline) {
  Guard guard;
  auto params = params_for(linux_platform(), TransportConfidence::Strong, false, false);
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  for (td::int32 t = kUnixTime; t < kUnixTime + 128; t++) {
    auto legacy = pick_runtime_profile("contract.example.com", t, linux_platform());
    auto adaptive = pick_runtime_profile_adaptive("contract.example.com", t, linux_platform(), EchMode::Disabled);
    ASSERT_TRUE(adaptive.profile == legacy);
    ASSERT_FALSE(adaptive.avoided_quarantined_profile);
    ASSERT_EQ(0u, adaptive.quarantined_candidate_count);
  }
}

// 2. The adaptive selection never returns a profile outside the platform's
//    allowed set, for every platform and a range of destinations/times.
TEST(RuntimeProfileRotationContract, AdaptiveStaysInsidePlatformAllowedSet) {
  Guard guard;
  const RuntimePlatformHints platforms[] = {ios_platform(), linux_platform(), windows_platform(), darwin_platform()};
  for (const auto &platform : platforms) {
    auto params = params_for(platform, TransportConfidence::Strong, false, true);
    ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());
    for (td::int32 t = kUnixTime; t < kUnixTime + 64; t++) {
      auto decision = pick_runtime_profile_adaptive("inside-" + td::to_string(t) + ".example", t, platform,
                                                    EchMode::Disabled);
      ASSERT_TRUE(platform_allows(platform, decision.profile));
    }
  }
}

// 3. One adaptive selection reports both the BrowserProfile and the final
//    hello_uses_ech, consistent with the profile's ECH capability and the route
//    ECH decision.
TEST(RuntimeProfileRotationContract, AdaptiveDecisionReportsConsistentWireVariant) {
  Guard guard;
  auto params = params_for(linux_platform(), TransportConfidence::Strong, false, false);
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  auto disabled = pick_runtime_profile_adaptive("variant.example.com", kUnixTime, linux_platform(), EchMode::Disabled);
  ASSERT_FALSE(disabled.hello_uses_ech);

  auto outer = pick_runtime_profile_adaptive("variant.example.com", kUnixTime, linux_platform(), EchMode::Rfc9180Outer);
  // hello_uses_ech is true iff the selected profile permits ECH and the route
  // resolved to RFC 9180 outer.
  ASSERT_EQ(profile_spec(outer.profile).allows_ech, outer.hello_uses_ech);
}

// 4. iOS Unknown + release-mode gating is valid only because the verified Apple
//    iOS TLS lane exists with TlsOnly + release_gating metadata and a non-zero
//    effective weight; selection then resolves to it, never advisory IOS14.
TEST(RuntimeProfileRotationContract, IosUnknownReleaseClosedOnlyByAppleIosTls) {
  Guard guard;

  // Metadata is genuinely verified browser-capture, TlsOnly, release-gated.
  const auto &apple = profile_fixture_metadata(BrowserProfile::AppleIosTls);
  ASSERT_TRUE(apple.source_kind == ProfileFixtureSourceKind::BrowserCapture);
  ASSERT_TRUE(apple.trust_tier == ProfileTrustTier::Verified);
  ASSERT_TRUE(apple.has_independent_network_provenance);
  ASSERT_TRUE(apple.release_gating);
  ASSERT_TRUE(apple.transport_claim_level == TransportClaimLevel::TlsOnly);
  ASSERT_TRUE(default_runtime_stealth_params().profile_weights.apple_ios_tls > 0);

  auto params = params_for(ios_platform(), TransportConfidence::Unknown, true, false);
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());
  for (int i = 0; i < 64; i++) {
    auto decision = pick_runtime_profile_adaptive("ios-rel-" + td::to_string(i) + ".example", kUnixTime + i,
                                                  ios_platform(), EchMode::Disabled);
    ASSERT_TRUE(decision.profile == BrowserProfile::AppleIosTls);
  }
}

}  // namespace
