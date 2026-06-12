// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::DesktopOs;
using td::mtproto::stealth::DeviceClass;
using td::mtproto::stealth::MobileOs;
using td::mtproto::stealth::pick_runtime_profile;
using td::mtproto::stealth::reset_runtime_stealth_params_for_tests;
using td::mtproto::stealth::RuntimePlatformHints;
using td::mtproto::stealth::set_runtime_stealth_params_for_tests;
using td::mtproto::stealth::StealthRuntimeParams;
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

RuntimePlatformHints windows_platform() {
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Desktop;
  platform.desktop_os = DesktopOs::Windows;
  return platform;
}

RuntimePlatformHints ios_platform() {
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Mobile;
  platform.mobile_os = MobileOs::IOS;
  return platform;
}

TEST(TlsRuntimeTransportConfidenceUnknownAdversarial, WindowsNeverEscapesTlsOnlyProfileUnderCrossLayerPressure) {
  RuntimeParamsGuard guard;

  StealthRuntimeParams params;
  params.transport_confidence = TransportConfidence::Unknown;
  params.platform_hints = windows_platform();
  params.profile_weights.chrome147_windows = 255;
  params.profile_weights.firefox149_windows = 1;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  for (td::uint32 i = 0; i < 4096; i++) {
    auto unix_time = static_cast<td::int32>(1712345678 + i * 131);
    td::string domain = "unknown-conf-windows-" + td::to_string(i) + ".example";
    auto profile = pick_runtime_profile(domain, unix_time, params.platform_hints);
    ASSERT_TRUE(profile == BrowserProfile::Firefox149_Windows);
  }
}

TEST(TlsRuntimeTransportConfidenceUnknownAdversarial, IosNeverEscapesTlsOnlyProfileUnderCrossLayerPressure) {
  RuntimeParamsGuard guard;

  StealthRuntimeParams params;
  params.transport_confidence = TransportConfidence::Unknown;
  params.platform_hints = ios_platform();
  params.profile_weights.ios14 = 1;
  params.profile_weights.chrome147_ios_chromium = 255;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  for (td::uint32 i = 0; i < 4096; i++) {
    auto unix_time = static_cast<td::int32>(1712345678 + i * 137);
    td::string domain = "unknown-conf-ios-" + td::to_string(i) + ".example";
    auto profile = pick_runtime_profile(domain, unix_time, params.platform_hints);
    // iOS at Unknown confidence now exposes both the advisory IOS14 lane and the
    // verified Apple iOS TLS lane (both TlsOnly); neither cross-layer lane appears.
    ASSERT_TRUE(profile == BrowserProfile::IOS14 || profile == BrowserProfile::AppleIosTls);
  }
}

}  // namespace
