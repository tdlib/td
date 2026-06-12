// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::DesktopOs;
using td::mtproto::stealth::DeviceClass;
using td::mtproto::stealth::pick_runtime_profile;
using td::mtproto::stealth::reset_runtime_stealth_params_for_tests;
using td::mtproto::stealth::RuntimePlatformHints;
using td::mtproto::stealth::set_runtime_stealth_params_for_tests;

class RuntimeParamsGuard final {
 public:
  RuntimeParamsGuard() {
    reset_runtime_stealth_params_for_tests();
  }

  ~RuntimeParamsGuard() {
    reset_runtime_stealth_params_for_tests();
  }
};

RuntimePlatformHints make_desktop_platform(DesktopOs desktop_os) {
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Desktop;
  platform.desktop_os = desktop_os;
  return platform;
}

TEST(TlsRuntimePlatformLaneStress, LinuxDesktopNeverBleedsIntoWindowsOrMobileProfilesEvenWhenWeighted) {
  RuntimeParamsGuard guard;

  auto params = td::mtproto::stealth::default_runtime_stealth_params();
  params.profile_weights.chrome133 = 0;
  params.profile_weights.chrome131 = 0;
  params.profile_weights.chrome120 = 0;
  params.profile_weights.firefox148 = 1;
  params.profile_weights.safari26_3 = 0;
  params.profile_weights.chrome147_windows = 100;
  params.profile_weights.firefox149_windows = 100;
  params.profile_weights.ios14 = 100;
  params.profile_weights.android_chromium_alps = 100;
  params.profile_weights.android11_okhttp_advisory = 100;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  auto linux_platform = make_desktop_platform(DesktopOs::Linux);
  for (td::uint32 idx = 0; idx < 4096; idx++) {
    auto unix_time = static_cast<td::int32>(1712440000 + idx);
    auto domain = "linux-lane-" + td::to_string(idx) + ".example";
    auto profile = pick_runtime_profile(domain, unix_time, linux_platform);
    ASSERT_TRUE(profile != BrowserProfile::Chrome147_Windows);
    ASSERT_TRUE(profile != BrowserProfile::Firefox149_Windows);
    ASSERT_TRUE(profile != BrowserProfile::Firefox149_MacOS26_3);
    ASSERT_TRUE(profile != BrowserProfile::Safari26_3);
    ASSERT_TRUE(profile != BrowserProfile::IOS14);
    ASSERT_TRUE(profile != BrowserProfile::Chrome147_IOSChromium);
    ASSERT_TRUE(profile != BrowserProfile::AndroidChromium_Alps);
    ASSERT_TRUE(profile != BrowserProfile::Firefox149_Android);
    ASSERT_TRUE(profile != BrowserProfile::Android11_OkHttp_Advisory);
  }
}

TEST(TlsRuntimePlatformLaneStress, WindowsDesktopNeverBleedsIntoAppleOrMobileProfiles) {
  RuntimeParamsGuard guard;

  auto params = td::mtproto::stealth::default_runtime_stealth_params();
  params.profile_weights.safari26_3 = 100;
  params.profile_weights.ios14 = 100;
  params.profile_weights.android_chromium_alps = 100;
  params.profile_weights.android11_okhttp_advisory = 100;
  params.profile_weights.chrome147_windows = 100;
  params.profile_weights.firefox149_windows = 100;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  auto windows_platform = make_desktop_platform(DesktopOs::Windows);
  for (td::uint32 idx = 0; idx < 4096; idx++) {
    auto unix_time = static_cast<td::int32>(1712550000 + idx);
    auto domain = "windows-lane-" + td::to_string(idx) + ".example";
    auto profile = pick_runtime_profile(domain, unix_time, windows_platform);
    ASSERT_TRUE(profile != BrowserProfile::Safari26_3);
    ASSERT_TRUE(profile != BrowserProfile::Firefox149_MacOS26_3);
    ASSERT_TRUE(profile != BrowserProfile::IOS14);
    ASSERT_TRUE(profile != BrowserProfile::Chrome147_IOSChromium);
    ASSERT_TRUE(profile != BrowserProfile::AndroidChromium_Alps);
    ASSERT_TRUE(profile != BrowserProfile::Firefox149_Android);
    ASSERT_TRUE(profile != BrowserProfile::Android11_OkHttp_Advisory);
  }
}

}  // namespace
