// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

// RISK_REGISTER:
// RISK_ID: runtime_platform_weight_gate_ios_lane_block
//   location: validate_allowed_profile_weights_for_platform in StealthRuntimeParams.cpp
//   category: configuration integrity / platform-lane coherence
//   attack: provide iOS runtime params where IOS14 weight is zero but
//           Chrome147_IOSChromium is fully enabled.
//   impact: valid allowed-lane configs may be rejected, forcing stale runtime
//           snapshots and cross-subsystem divergence from operator policy.
//   test_ids: TlsRuntimePlatformWeightGateAdversarial.IosChromiumOnlyLaneCanBePublished

#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

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

class RuntimeParamsGuard final {
 public:
  RuntimeParamsGuard() {
    reset_runtime_stealth_params_for_tests();
  }

  ~RuntimeParamsGuard() {
    reset_runtime_stealth_params_for_tests();
  }
};

RuntimePlatformHints ios_platform() {
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Mobile;
  platform.mobile_os = MobileOs::IOS;
  platform.desktop_os = DesktopOs::Unknown;
  return platform;
}

RuntimePlatformHints windows_platform() {
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Desktop;
  platform.mobile_os = MobileOs::None;
  platform.desktop_os = DesktopOs::Windows;
  return platform;
}

TEST(TlsRuntimePlatformWeightGateAdversarial, IosChromiumOnlyLaneCanBePublished) {
  RuntimeParamsGuard guard;

  auto params = default_runtime_stealth_params();
  params.transport_confidence = TransportConfidence::Partial;
  params.platform_hints = ios_platform();

  params.profile_weights.ios14 = 0;
  params.profile_weights.chrome147_ios_chromium = 100;
  params.profile_weights.apple_ios_tls = 0;

  auto status = set_runtime_stealth_params_for_tests(params);
  ASSERT_TRUE(status.is_ok());

  for (td::uint32 i = 0; i < 128; i++) {
    auto unix_time = static_cast<td::int32>(1712345678 + i * 17);
    td::string domain = "ios-chromium-only-" + td::to_string(i) + ".example.com";
    auto profile = pick_runtime_profile(domain, unix_time, params.platform_hints);
    ASSERT_TRUE(profile == BrowserProfile::Chrome147_IOSChromium);
  }
}

TEST(TlsRuntimePlatformWeightGateAdversarial, IosChromiumOnlyLaneWithZeroAndroidCanBePublished) {
  RuntimeParamsGuard guard;

  auto params = default_runtime_stealth_params();
  params.transport_confidence = TransportConfidence::Partial;
  params.platform_hints = ios_platform();

  params.profile_weights.ios14 = 0;
  params.profile_weights.chrome147_ios_chromium = 100;
  params.profile_weights.apple_ios_tls = 0;
  params.profile_weights.android11_okhttp_advisory = 0;

  auto status = set_runtime_stealth_params_for_tests(params);
  ASSERT_TRUE(status.is_ok());

  for (td::uint32 i = 0; i < 128; i++) {
    auto unix_time = static_cast<td::int32>(1712345678 + i * 17);
    td::string domain = "ios-chromium-only-no-android-" + td::to_string(i) + ".example.com";
    auto profile = pick_runtime_profile(domain, unix_time, params.platform_hints);
    ASSERT_TRUE(profile == BrowserProfile::Chrome147_IOSChromium);
  }
}

TEST(TlsRuntimePlatformWeightGateAdversarial, WindowsExplicitLaneCanBePublishedWithoutLegacyNonDarwinDesktopWeights) {
  RuntimeParamsGuard guard;

  auto params = default_runtime_stealth_params();
  params.transport_confidence = TransportConfidence::Partial;
  params.platform_hints = windows_platform();

  params.profile_weights.chrome133 = 0;
  params.profile_weights.chrome131 = 0;
  params.profile_weights.chrome120 = 0;
  params.profile_weights.firefox148 = 0;
  params.profile_weights.safari26_3 = 0;

  params.profile_weights.chrome147_windows = 100;
  params.profile_weights.firefox149_windows = 0;

  auto status = set_runtime_stealth_params_for_tests(params);
  ASSERT_TRUE(status.is_ok());

  for (td::uint32 i = 0; i < 128; i++) {
    auto unix_time = static_cast<td::int32>(1715345678 + i * 31);
    td::string domain = "windows-explicit-lane-" + td::to_string(i) + ".example.com";
    auto profile = pick_runtime_profile(domain, unix_time, params.platform_hints);
    ASSERT_TRUE(profile == BrowserProfile::Chrome147_Windows);
  }
}

}  // namespace
