// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/tests.h"

namespace {

using td::int32;
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

td::string mutate_domain(td::uint32 i) {
  td::string domain = "FuZz-Unknown-" + td::to_string(i) + ".Example.com";
  if ((i % 3) == 0) {
    domain += ".";
  }
  if ((i % 5) == 0) {
    for (auto &ch : domain) {
      if ('a' <= ch && ch <= 'z') {
        ch = static_cast<char>(ch - 'a' + 'A');
      }
    }
  }
  return domain;
}

TEST(TlsRuntimeTransportConfidenceUnknownLightFuzz, WindowsAndIosNeverSelectCrossLayerProfiles) {
  RuntimeParamsGuard guard;

  StealthRuntimeParams windows_params;
  windows_params.transport_confidence = TransportConfidence::Unknown;
  windows_params.platform_hints = windows_platform();
  windows_params.profile_weights.chrome147_windows = 250;
  windows_params.profile_weights.firefox149_windows = 1;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(windows_params).is_ok());

  for (td::uint32 i = 0; i < 10000; i++) {
    auto unix_time = static_cast<int32>(1712345678 + static_cast<int32>(i * 17));
    auto profile = pick_runtime_profile(mutate_domain(i), unix_time, windows_params.platform_hints);
    ASSERT_TRUE(profile == BrowserProfile::Firefox149_Windows);
  }

  StealthRuntimeParams ios_params;
  ios_params.transport_confidence = TransportConfidence::Unknown;
  ios_params.platform_hints = ios_platform();
  ios_params.profile_weights.ios14 = 1;
  ios_params.profile_weights.chrome147_ios_chromium = 250;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(ios_params).is_ok());

  for (td::uint32 i = 0; i < 10000; i++) {
    auto unix_time = static_cast<int32>(1712345678 + static_cast<int32>(i * 19));
    auto profile = pick_runtime_profile(mutate_domain(i + 10000), unix_time, ios_params.platform_hints);
    // iOS at Unknown confidence now exposes both the advisory IOS14 lane and the
    // verified Apple iOS TLS lane (both TlsOnly).
    ASSERT_TRUE(profile == BrowserProfile::IOS14 || profile == BrowserProfile::AppleIosTls);
  }
}

}  // namespace
