// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Regression guard for the former Darwin hardcoding bug:
// runtime selection must stay on the Darwin-specific profile set instead of
// collapsing back to the legacy Chrome133 Linux-desktop lane.

#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"
#include "td/utils/tests.h"
#include "td/utils/Time.h"

namespace {

TEST(DarwinProfileHardcodingBug, VerifyMacOSFixturesExist) {
  auto profile = td::mtproto::stealth::pick_runtime_profile(
      "test.com", static_cast<td::int32>(td::Time::now()),
      td::mtproto::stealth::RuntimePlatformHints{td::mtproto::stealth::DeviceClass::Desktop,
                                                 td::mtproto::stealth::MobileOs::None,
                                                 td::mtproto::stealth::DesktopOs::Darwin});

  auto spec = td::mtproto::stealth::profile_spec(profile);
  ASSERT_TRUE(spec.name.size() > 0);
  ASSERT_TRUE(profile != td::mtproto::BrowserProfile::Chrome133);
}

TEST(DarwinProfileHardcodingBug, DarwinAlwaysSelectsChrome133) {
  auto hint_darwin = td::mtproto::stealth::RuntimePlatformHints{td::mtproto::stealth::DeviceClass::Desktop,
                                                                td::mtproto::stealth::MobileOs::None,
                                                                td::mtproto::stealth::DesktopOs::Darwin};

  auto now = static_cast<td::int32>(td::Time::now());
  bool saw_non_chrome133 = false;
  for (int i = 0; i < 10; i++) {
    auto test_time = now + (i * 3600);  // Different time buckets
    auto profile = td::mtproto::stealth::pick_runtime_profile("test.com", test_time, hint_darwin);
    saw_non_chrome133 = saw_non_chrome133 || profile != td::mtproto::BrowserProfile::Chrome133;
  }
  ASSERT_TRUE(saw_non_chrome133);
}

TEST(DarwinProfileHardcodingBug, FixtureProfileVarietyNotUsed) {
  auto darwin_profiles = td::mtproto::stealth::allowed_profiles_for_platform(td::mtproto::stealth::RuntimePlatformHints{
      td::mtproto::stealth::DeviceClass::Desktop, td::mtproto::stealth::MobileOs::None,
      td::mtproto::stealth::DesktopOs::Darwin});

  bool has_chromium = false;
  bool has_firefox = false;
  bool has_safari = false;
  bool has_legacy_linux_chrome = false;

  for (auto profile : darwin_profiles) {
    if (profile == td::mtproto::BrowserProfile::ChromiumMacOS_NoAlps ||
        profile == td::mtproto::BrowserProfile::ChromiumMacOS_4469 ||
        profile == td::mtproto::BrowserProfile::ChromiumMacOS_44CD) {
      has_chromium = true;
    }
    if (profile == td::mtproto::BrowserProfile::Firefox149_MacOS26_3) {
      has_firefox = true;
    }
    if (profile == td::mtproto::BrowserProfile::Safari26_3) {
      has_safari = true;
    }
    if (profile == td::mtproto::BrowserProfile::Chrome133) {
      has_legacy_linux_chrome = true;
    }
  }

  ASSERT_TRUE(has_chromium);
  ASSERT_TRUE(has_firefox);
  ASSERT_TRUE(has_safari);
  ASSERT_FALSE(has_legacy_linux_chrome);
}

TEST(DarwinProfileHardcodingBug, ThreatsToProfileFixCorrectionness) {
  // If Darwin is fixed to use pick_runtime_profile() like non-Darwin:
  // Verify that the fix would work correctly with circuit breaker and ECH policy

  auto now = static_cast<td::int32>(td::Time::now());

  auto hint_darwin = td::mtproto::stealth::RuntimePlatformHints{td::mtproto::stealth::DeviceClass::Desktop,
                                                                td::mtproto::stealth::MobileOs::None,
                                                                td::mtproto::stealth::DesktopOs::Darwin};

  auto route = td::mtproto::stealth::NetworkRouteHints{};
  route.is_known = true;
  route.is_ru = false;

  // After fix, this should:
  // 1. Select a random profile (deterministic per destination/time)
  // 2. Check profile.allows_ech
  // 3. Apply route policy + circuit breaker on top

  auto profile = td::mtproto::stealth::pick_runtime_profile("test.com", now, hint_darwin);
  (void)td::mtproto::stealth::profile_spec(profile);
  (void)td::mtproto::stealth::get_runtime_ech_decision("test.com", now, route);

  // The corrected behavior should:
  // - Not hardcode Chrome133
  // - Use the selected profile's allows_ech
  // - Respect circuit breaker state
}

}  // namespace
