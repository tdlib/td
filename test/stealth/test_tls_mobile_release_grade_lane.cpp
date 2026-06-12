// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Regression for PR #21 review finding 2 (F2): the verified browser-capture iOS
// Chromium lane (Chrome147_IOSChromium) was pinned to weight 0 in the effective
// profile weights, so iOS had only the advisory utls IOS14 lane and Android only
// its advisory utls okhttp lane. The effective weights now carve a slice of the
// iOS share for the verified iOS Chromium lane, making it reachable once
// transport_confidence permits its cross-layer claim.
//
// The iOS/default Unknown release-grade gap is now closed by the verified
// browser-capture Apple iOS TLS lane (AppleIosTls): it is TlsOnly + release-gated,
// so at Unknown transport_confidence iOS is no longer limited to the advisory
// IOS14 lane — AppleIosTls is reachable alongside it, while the cross-layer-claim
// Chrome147_IOSChromium lane stays unreachable without confidence evidence.
// Android still carries a reviewed ALPS-bearing Chromium lane that must remain
// unreachable at Unknown confidence so the runtime stays fail-closed onto the
// advisory okhttp fallback.

#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::default_runtime_stealth_params;
using td::mtproto::stealth::DesktopOs;
using td::mtproto::stealth::DeviceClass;
using td::mtproto::stealth::MobileOs;
using td::mtproto::stealth::pick_runtime_profile;
using td::mtproto::stealth::reset_runtime_stealth_params_for_tests;
using td::mtproto::stealth::RuntimePlatformHints;
using td::mtproto::stealth::set_runtime_stealth_params_for_tests;
using td::mtproto::stealth::TransportConfidence;

constexpr td::int32 kUnixTime = 1712345678;

class Guard final {
 public:
  Guard() {
    reset_runtime_stealth_params_for_tests();
  }
  ~Guard() {
    reset_runtime_stealth_params_for_tests();
  }
};

RuntimePlatformHints ios_platform() {
  return RuntimePlatformHints{DeviceClass::Mobile, MobileOs::IOS, DesktopOs::Unknown};
}

RuntimePlatformHints android_platform() {
  return RuntimePlatformHints{DeviceClass::Mobile, MobileOs::Android, DesktopOs::Unknown};
}

// The verified iOS Chromium lane now carries a non-zero effective weight (a slice
// of the iOS share), instead of the previous hardcoded 0.
TEST(MobileReleaseGradeLane, IosChromiumLaneHasNonZeroEffectiveWeight) {
  Guard guard;
  auto weights = default_runtime_stealth_params().profile_weights;
  ASSERT_TRUE(weights.chrome147_ios_chromium > 0);
  ASSERT_TRUE(weights.apple_ios_tls > 0);
  ASSERT_TRUE(weights.ios14 > 0);
  // The carve-outs come out of the iOS share: ios14 + chrome147_ios_chromium +
  // apple_ios_tls equals the configured iOS weight (70 by default).
  ASSERT_EQ(70, weights.ios14 + weights.chrome147_ios_chromium + weights.apple_ios_tls);
}

// With transport_confidence established, iOS can reach the verified Chromium lane
// while the advisory IOS14 lane stays dominant.
TEST(MobileReleaseGradeLane, IosReachesVerifiedChromiumLaneAtEstablishedConfidence) {
  Guard guard;
  auto params = default_runtime_stealth_params();
  params.transport_confidence = TransportConfidence::Partial;
  params.platform_hints = ios_platform();
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  bool saw_chromium = false;
  bool saw_ios14 = false;
  for (int i = 0; i < 256 && !(saw_chromium && saw_ios14); i++) {
    auto profile = pick_runtime_profile("ios-rel-" + td::to_string(i) + ".example", kUnixTime + i, ios_platform());
    if (profile == BrowserProfile::Chrome147_IOSChromium) {
      saw_chromium = true;
    } else if (profile == BrowserProfile::IOS14) {
      saw_ios14 = true;
    }
  }
  ASSERT_TRUE(saw_chromium);
  ASSERT_TRUE(saw_ios14);
}

// At the default Unknown confidence iOS selects only TlsOnly-claim lanes — the
// advisory IOS14 lane and the verified Apple iOS TLS lane — never the
// cross-layer-claim Chrome147_IOSChromium lane (which needs confidence evidence).
// The verified Apple iOS TLS lane is reachable here, so IOS14 is no longer the
// only Unknown-confidence iOS lane (the closed iOS/default release-grade gap).
TEST(MobileReleaseGradeLane, IosUnknownConfidenceSelectsOnlyTlsOnlyLanes) {
  Guard guard;
  auto params = default_runtime_stealth_params();
  params.transport_confidence = TransportConfidence::Unknown;
  params.platform_hints = ios_platform();
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  bool saw_apple_ios_tls = false;
  for (int i = 0; i < 256; i++) {
    auto profile = pick_runtime_profile("ios-unk-" + td::to_string(i) + ".example", kUnixTime + i, ios_platform());
    ASSERT_TRUE(profile == BrowserProfile::IOS14 || profile == BrowserProfile::AppleIosTls);
    if (profile == BrowserProfile::AppleIosTls) {
      saw_apple_ios_tls = true;
    }
  }
  ASSERT_TRUE(saw_apple_ios_tls);
}

TEST(MobileReleaseGradeLane, AndroidChromiumLaneHasNonZeroEffectiveWeight) {
  Guard guard;
  auto weights = default_runtime_stealth_params().profile_weights;
  ASSERT_TRUE(weights.android_chromium_alps > 0);
  ASSERT_TRUE(weights.firefox149_android > 0);
  ASSERT_TRUE(weights.android11_okhttp_advisory > 0);
  ASSERT_EQ(30, weights.android_chromium_alps + weights.firefox149_android + weights.android11_okhttp_advisory);
}

TEST(MobileReleaseGradeLane, AndroidReachesVerifiedChromiumLaneAtEstablishedConfidence) {
  Guard guard;
  auto params = default_runtime_stealth_params();
  params.transport_confidence = TransportConfidence::Partial;
  params.platform_hints = android_platform();
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  bool saw_android_chromium = false;
  bool saw_android_firefox = false;
  bool saw_advisory = false;
  for (int i = 0; i < 512 && !(saw_android_chromium && saw_android_firefox && saw_advisory); i++) {
    auto profile =
        pick_runtime_profile("android-rel-" + td::to_string(i) + ".example", kUnixTime + i, android_platform());
    if (profile == BrowserProfile::AndroidChromium_Alps) {
      saw_android_chromium = true;
    } else if (profile == BrowserProfile::Firefox149_Android) {
      saw_android_firefox = true;
    } else if (profile == BrowserProfile::Android11_OkHttp_Advisory) {
      saw_advisory = true;
    }
  }
  ASSERT_TRUE(saw_android_chromium);
  ASSERT_TRUE(saw_android_firefox);
  ASSERT_TRUE(saw_advisory);
}

TEST(MobileReleaseGradeLane, AndroidDefaultsToAdvisoryLaneAtUnknownConfidence) {
  Guard guard;
  auto params = default_runtime_stealth_params();
  params.transport_confidence = TransportConfidence::Unknown;
  params.platform_hints = android_platform();
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  for (int i = 0; i < 128; i++) {
    auto profile =
        pick_runtime_profile("android-unk-" + td::to_string(i) + ".example", kUnixTime + i, android_platform());
    ASSERT_TRUE(profile == BrowserProfile::Android11_OkHttp_Advisory);
  }
}

}  // namespace
