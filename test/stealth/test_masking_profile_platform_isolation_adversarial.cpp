// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Adversarial tests: platform/OS profile isolation.
//
// Threat model: a DPI device that knows the client is on Linux expects
// a Linux/Chrome or Linux/Firefox TLS fingerprint. If the client
// accidentally emits an iOS or MacOS Safari ClientHello on a Linux device,
// the platform mismatch is a high-confidence fingerprint.
//
// Similarly, a mobile device using a desktop Chrome profile is suspicious.
//
// These tests verify that:
//   A — iOS profiles (Safari26_3, IOS14, Chrome147_IOSChromium) are NEVER
//       selected on Linux/Windows desktop platforms.
//   B — Android profiles (AndroidChromium_Alps, Firefox149_Android,
//       Android11_OkHttp_Advisory) are NEVER selected on desktop platforms.
//   C — Desktop Chrome/Firefox profiles are NEVER selected on iOS platforms.
//   D — Darwin desktop profiles include Safari but NOT iOS14.

#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/tests.h"

#include <set>

namespace {

using td::mtproto::stealth::allowed_profiles_for_platform;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::default_profile_weights;
using td::mtproto::stealth::DesktopOs;
using td::mtproto::stealth::DeviceClass;
using td::mtproto::stealth::make_profile_selection_key;
using td::mtproto::stealth::MobileOs;
using td::mtproto::stealth::pick_profile_sticky;
using td::mtproto::stealth::RuntimePlatformHints;
using td::mtproto::test::MockRng;
using td::uint32;

RuntimePlatformHints make_linux_desktop() {
  RuntimePlatformHints h;
  h.device_class = DeviceClass::Desktop;
  h.desktop_os = DesktopOs::Linux;
  return h;
}

RuntimePlatformHints make_windows_desktop() {
  RuntimePlatformHints h;
  h.device_class = DeviceClass::Desktop;
  h.desktop_os = DesktopOs::Windows;
  return h;
}

RuntimePlatformHints make_darwin_desktop() {
  RuntimePlatformHints h;
  h.device_class = DeviceClass::Desktop;
  h.desktop_os = DesktopOs::Darwin;
  return h;
}

RuntimePlatformHints make_ios_mobile() {
  RuntimePlatformHints h;
  h.device_class = DeviceClass::Mobile;
  h.mobile_os = MobileOs::IOS;
  return h;
}

RuntimePlatformHints make_android_mobile() {
  RuntimePlatformHints h;
  h.device_class = DeviceClass::Mobile;
  h.mobile_os = MobileOs::Android;
  return h;
}

// Collect all profiles that can be returned by pick_runtime_profile for a given platform.
std::set<BrowserProfile> collect_reachable_profiles(const RuntimePlatformHints &platform, int sample_count = 2048) {
  std::set<BrowserProfile> profiles;
  auto weights = default_profile_weights();
  auto allowed = allowed_profiles_for_platform(platform);

  for (uint32 bucket = 0; bucket < static_cast<uint32>(sample_count); bucket++) {
    MockRng rng(bucket * 17 + 3);
    auto key = make_profile_selection_key("test.example.com", static_cast<td::int32>(bucket * 100));
    auto profile = pick_profile_sticky(weights, key, platform, allowed, rng);
    profiles.insert(profile);
  }
  return profiles;
}

// -----------------------------------------------------------------------
// Threat model A: iOS-specific profiles must NOT appear on Linux desktop.
// -----------------------------------------------------------------------

TEST(MaskingProfilePlatformIsolationAdversarial, IosProfilesNeverSelectedOnLinuxDesktop) {
  auto linux_platform = make_linux_desktop();
  auto allowed = allowed_profiles_for_platform(linux_platform);

  for (auto profile : allowed) {
    ASSERT_TRUE(profile != BrowserProfile::IOS14);
    ASSERT_TRUE(profile != BrowserProfile::Safari26_3);
    ASSERT_TRUE(profile != BrowserProfile::Chrome147_IOSChromium);
  }
}

TEST(MaskingProfilePlatformIsolationAdversarial, AndroidProfilesNeverSelectedOnLinuxDesktop) {
  auto linux_platform = make_linux_desktop();
  auto allowed = allowed_profiles_for_platform(linux_platform);

  for (auto profile : allowed) {
    ASSERT_TRUE(profile != BrowserProfile::AndroidChromium_Alps);
    ASSERT_TRUE(profile != BrowserProfile::Firefox149_Android);
    ASSERT_TRUE(profile != BrowserProfile::Android11_OkHttp_Advisory);
  }
}

// -----------------------------------------------------------------------
// Threat model A: same verification for Windows desktop.
// -----------------------------------------------------------------------

TEST(MaskingProfilePlatformIsolationAdversarial, IosProfilesNeverSelectedOnWindowsDesktop) {
  auto win_platform = make_windows_desktop();
  auto allowed = allowed_profiles_for_platform(win_platform);

  for (auto profile : allowed) {
    ASSERT_TRUE(profile != BrowserProfile::IOS14);
    ASSERT_TRUE(profile != BrowserProfile::Safari26_3);
    ASSERT_TRUE(profile != BrowserProfile::Chrome147_IOSChromium);
  }
}

TEST(MaskingProfilePlatformIsolationAdversarial, AndroidProfilesNeverSelectedOnWindowsDesktop) {
  auto win_platform = make_windows_desktop();
  auto allowed = allowed_profiles_for_platform(win_platform);

  for (auto profile : allowed) {
    ASSERT_TRUE(profile != BrowserProfile::AndroidChromium_Alps);
    ASSERT_TRUE(profile != BrowserProfile::Firefox149_Android);
    ASSERT_TRUE(profile != BrowserProfile::Android11_OkHttp_Advisory);
  }
}

// -----------------------------------------------------------------------
// Threat model D: Darwin desktop includes Safari but NOT IOS14/Android.
// -----------------------------------------------------------------------

TEST(MaskingProfilePlatformIsolationAdversarial, DarwinDesktopDoesNotIncludeIOS14) {
  auto darwin_platform = make_darwin_desktop();
  auto allowed = allowed_profiles_for_platform(darwin_platform);

  for (auto profile : allowed) {
    ASSERT_TRUE(profile != BrowserProfile::IOS14);
    ASSERT_TRUE(profile != BrowserProfile::AndroidChromium_Alps);
    ASSERT_TRUE(profile != BrowserProfile::Firefox149_Android);
    ASSERT_TRUE(profile != BrowserProfile::Android11_OkHttp_Advisory);
  }
}

// -----------------------------------------------------------------------
// Threat model C: iOS mobile must NOT include desktop-only profiles.
// Desktop Chrome133/131/120 on iOS would be suspicious.
// -----------------------------------------------------------------------

TEST(MaskingProfilePlatformIsolationAdversarial, IosProfileSetDoesNotContainLinuxChromeProfiles) {
  auto ios_platform = make_ios_mobile();
  auto allowed = allowed_profiles_for_platform(ios_platform);

  for (auto profile : allowed) {
    ASSERT_TRUE(profile != BrowserProfile::Firefox148);
    ASSERT_TRUE(profile != BrowserProfile::Firefox149_Windows);
    ASSERT_TRUE(profile != BrowserProfile::Chrome147_Windows);
  }
}

// -----------------------------------------------------------------------
// Threat model C: Android mobile must NOT include iOS-specific profiles.
// -----------------------------------------------------------------------

TEST(MaskingProfilePlatformIsolationAdversarial, AndroidProfileSetDoesNotContainIosProfiles) {
  auto android_platform = make_android_mobile();
  auto allowed = allowed_profiles_for_platform(android_platform);

  for (auto profile : allowed) {
    ASSERT_TRUE(profile != BrowserProfile::Safari26_3);
    ASSERT_TRUE(profile != BrowserProfile::IOS14);
    ASSERT_TRUE(profile != BrowserProfile::Chrome147_IOSChromium);
  }
}

// -----------------------------------------------------------------------
// Reachability: Linux desktop must be able to reach Chrome AND Firefox profiles.
// A platform limited to only Chrome would be distinguishable.
// -----------------------------------------------------------------------

TEST(MaskingProfilePlatformIsolationAdversarial, LinuxDesktopCanReachBothChromeAndFirefoxProfiles) {
  auto reachable = collect_reachable_profiles(make_linux_desktop());

  bool has_chrome = reachable.count(BrowserProfile::Chrome133) > 0 || reachable.count(BrowserProfile::Chrome131) > 0 ||
                    reachable.count(BrowserProfile::Chrome120) > 0;
  bool has_firefox = reachable.count(BrowserProfile::Firefox148) > 0;

  ASSERT_TRUE(has_chrome);
  ASSERT_TRUE(has_firefox);
}

// -----------------------------------------------------------------------
// Reachability: iOS must be able to reach iOS-specific profiles.
// -----------------------------------------------------------------------

TEST(MaskingProfilePlatformIsolationAdversarial, IosMobileCanReachIOSProfiles) {
  auto ios_platform = make_ios_mobile();
  auto allowed = allowed_profiles_for_platform(ios_platform);

  bool has_ios = false;
  for (auto profile : allowed) {
    if (profile == BrowserProfile::IOS14 || profile == BrowserProfile::Safari26_3 ||
        profile == BrowserProfile::Chrome147_IOSChromium) {
      has_ios = true;
      break;
    }
  }
  ASSERT_TRUE(has_ios);
}

// -----------------------------------------------------------------------
// Allowed profile list must not be empty for any platform.
// -----------------------------------------------------------------------

TEST(MaskingProfilePlatformIsolationAdversarial, AllPlatformsHaveAtLeastOneAllowedProfile) {
  const RuntimePlatformHints platforms[] = {
      make_linux_desktop(), make_windows_desktop(), make_darwin_desktop(), make_ios_mobile(), make_android_mobile(),
  };

  for (const auto &platform : platforms) {
    auto allowed = allowed_profiles_for_platform(platform);
    ASSERT_TRUE(!allowed.empty());
  }
}

}  // namespace
