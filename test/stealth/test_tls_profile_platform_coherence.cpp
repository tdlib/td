// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::allowed_profiles_for_platform;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::default_profile_weights;
using td::mtproto::stealth::DesktopOs;
using td::mtproto::stealth::DeviceClass;
using td::mtproto::stealth::MobileOs;
using td::mtproto::stealth::pick_profile_sticky;
using td::mtproto::stealth::RuntimePlatformHints;
using td::mtproto::stealth::SelectionKey;
using td::mtproto::test::MockRng;

SelectionKey make_selection_key(td::uint32 bucket) {
  SelectionKey key;
  key.destination = "mobile.example.com";
  key.time_bucket = bucket;
  return key;
}

TEST(TlsProfilePlatformCoherence, AndroidRuntimeSelectionNeverUsesIosProfile) {
  MockRng rng(101);
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Mobile;
  platform.mobile_os = MobileOs::Android;

  auto allowed = allowed_profiles_for_platform(platform);
  for (td::uint32 bucket = 20000; bucket < 20256; bucket++) {
    auto profile = pick_profile_sticky(default_profile_weights(), make_selection_key(bucket), platform, allowed, rng);
    ASSERT_TRUE(BrowserProfile::Android11_OkHttp_Advisory == profile);
  }
}

TEST(TlsProfilePlatformCoherence, IosRuntimeSelectionNeverUsesAndroidProfile) {
  MockRng rng(202);
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Mobile;
  platform.mobile_os = MobileOs::IOS;

  auto allowed = allowed_profiles_for_platform(platform);
  for (td::uint32 bucket = 20000; bucket < 20256; bucket++) {
    auto profile = pick_profile_sticky(default_profile_weights(), make_selection_key(bucket), platform, allowed, rng);
    ASSERT_TRUE(profile != BrowserProfile::Android11_OkHttp_Advisory);
  }
}

TEST(TlsProfilePlatformCoherence, DesktopSelectionNeverFallsIntoMobileProfilesEvenWithUnknownDesktopOs) {
  MockRng rng(303);
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Desktop;
  platform.desktop_os = DesktopOs::Unknown;

  auto allowed = allowed_profiles_for_platform(platform);
  for (td::uint32 bucket = 20000; bucket < 20256; bucket++) {
    auto profile = pick_profile_sticky(default_profile_weights(), make_selection_key(bucket), platform, allowed, rng);
    ASSERT_TRUE(profile != BrowserProfile::IOS14);
    ASSERT_TRUE(profile != BrowserProfile::Android11_OkHttp_Advisory);
  }
}

TEST(TlsProfilePlatformCoherence, DarwinAllowedProfilesUseMacosFirefoxInsteadOfLinuxFirefox) {
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Desktop;
  platform.desktop_os = DesktopOs::Darwin;

  auto allowed = allowed_profiles_for_platform(platform);
  bool saw_macos_firefox = false;
  for (auto profile : allowed) {
    ASSERT_TRUE(profile != BrowserProfile::Firefox148);
    if (profile == BrowserProfile::Firefox149_MacOS26_3) {
      saw_macos_firefox = true;
    }
  }
  ASSERT_TRUE(saw_macos_firefox);
}

}  // namespace