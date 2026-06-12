// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Adversarial tests: sticky profile selection weight boundaries.

#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <cstdio>
#include <limits>
#include <set>

namespace {

using td::mtproto::stealth::all_profiles;
using td::mtproto::stealth::allowed_profiles_for_platform;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::default_profile_weights;
using td::mtproto::stealth::DesktopOs;
using td::mtproto::stealth::DeviceClass;
using td::mtproto::stealth::make_profile_selection_key;
using td::mtproto::stealth::MobileOs;
using td::mtproto::stealth::pick_profile_sticky;
using td::mtproto::stealth::ProfileWeights;
using td::mtproto::stealth::RuntimePlatformHints;
using td::mtproto::stealth::SelectionKey;
using td::mtproto::test::MockRng;

// Local replica of private registry weighting logic.
// This intentionally mirrors td/mtproto/stealth/TlsHelloProfileRegistry.cpp
// so we can test weight sums via public API without exposing internals.
td::uint8 profile_weight_of(const ProfileWeights &weights, BrowserProfile profile) {
  switch (profile) {
    case BrowserProfile::Chrome133:
      return weights.chrome133;
    case BrowserProfile::Chrome131:
      return weights.chrome131;
    case BrowserProfile::Chrome120:
      return weights.chrome120;
    case BrowserProfile::Chrome147_Windows:
      return weights.chrome147_windows;
    case BrowserProfile::ChromiumMacOS_NoAlps:
      return weights.chromium_macos_no_alps;
    case BrowserProfile::ChromiumMacOS_4469:
      return weights.chromium_macos_4469;
    case BrowserProfile::ChromiumMacOS_44CD:
      return weights.chromium_macos_44cd;
    case BrowserProfile::Chrome147_IOSChromium:
      return weights.chrome147_ios_chromium;
    case BrowserProfile::Firefox148:
      return weights.firefox148;
    case BrowserProfile::Firefox149_Android:
      return weights.firefox149_android;
    case BrowserProfile::Firefox149_MacOS26_3:
      return weights.firefox149_macos26_3;
    case BrowserProfile::Firefox149_Windows:
      return weights.firefox149_windows;
    case BrowserProfile::Safari26_3:
      return weights.safari26_3;
    case BrowserProfile::IOS14:
      return weights.ios14;
    case BrowserProfile::Android11_OkHttp_Advisory:
      return weights.android11_okhttp_advisory;
    default:
      return 0;
  }
}

TEST(ProfileSelectionWeightAdversarial, DefaultWeightsHaveNonZeroTotalForAllPlatforms) {
  const RuntimePlatformHints platforms[] = {
      {DeviceClass::Desktop, MobileOs::None, DesktopOs::Unknown},
      {DeviceClass::Desktop, MobileOs::None, DesktopOs::Darwin},
      {DeviceClass::Desktop, MobileOs::None, DesktopOs::Windows},
      {DeviceClass::Desktop, MobileOs::None, DesktopOs::Linux},
      {DeviceClass::Mobile, MobileOs::IOS, DesktopOs::Unknown},
      {DeviceClass::Mobile, MobileOs::Android, DesktopOs::Unknown},
      {DeviceClass::Mobile, MobileOs::None, DesktopOs::Unknown},
  };

  auto weights = default_profile_weights();
  for (const auto &platform : platforms) {
    auto allowed = allowed_profiles_for_platform(platform);
    ASSERT_TRUE(!allowed.empty());
    td::uint32 total = 0;
    for (auto p : allowed) {
      total += profile_weight_of(weights, p);
    }
    ASSERT_TRUE(total > 0);
  }
}

TEST(ProfileSelectionWeightAdversarial, SingleAllowedProfileAlwaysSelected) {
  ProfileWeights w;
  w.chrome133 = 255;
  w.chrome131 = 0;
  w.chrome120 = 0;
  w.firefox148 = 0;
  w.safari26_3 = 0;
  w.ios14 = 0;
  w.chromium_macos_no_alps = 0;
  w.chromium_macos_4469 = 0;
  w.chromium_macos_44cd = 0;
  w.firefox149_android = 0;
  w.android11_okhttp_advisory = 0;
  w.chrome147_windows = 0;
  w.chrome147_ios_chromium = 0;
  w.firefox149_macos26_3 = 0;
  w.firefox149_windows = 0;
  w.android_chromium_alps = 0;

  const BrowserProfile only[] = {BrowserProfile::Chrome133};
  auto allowed = td::Span<BrowserProfile>(only);
  RuntimePlatformHints platform{};
  SelectionKey key;
  key.destination = "one.example.com";

  for (td::uint32 bucket = 0; bucket < 100; bucket++) {
    key.time_bucket = bucket;
    MockRng rng(static_cast<td::uint64>(bucket) + 1);
    auto picked = pick_profile_sticky(w, key, platform, allowed, rng);
    ASSERT_TRUE(picked == BrowserProfile::Chrome133);
  }
}

TEST(ProfileSelectionWeightAdversarial, ZeroWeightEntryIsNeverSelected) {
  ProfileWeights w;
  w.chrome133 = 0;
  w.chrome131 = 200;
  w.chrome120 = 0;
  w.firefox148 = 0;
  w.safari26_3 = 0;
  w.ios14 = 0;
  w.chromium_macos_no_alps = 0;
  w.chromium_macos_4469 = 0;
  w.chromium_macos_44cd = 0;
  w.firefox149_android = 0;
  w.android11_okhttp_advisory = 0;
  w.chrome147_windows = 0;
  w.chrome147_ios_chromium = 0;
  w.firefox149_macos26_3 = 0;
  w.firefox149_windows = 0;
  w.android_chromium_alps = 0;

  const BrowserProfile allowed_arr[] = {BrowserProfile::Chrome133, BrowserProfile::Chrome131};
  auto allowed = td::Span<BrowserProfile>(allowed_arr);
  RuntimePlatformHints platform{};
  SelectionKey key;
  key.destination = "zero-weight.example.com";

  for (td::uint32 bucket = 0; bucket < 500; bucket++) {
    key.time_bucket = bucket;
    MockRng rng(static_cast<td::uint64>(bucket) * 3 + 7);
    auto picked = pick_profile_sticky(w, key, platform, allowed, rng);
    ASSERT_TRUE(picked == BrowserProfile::Chrome131);
  }
}

TEST(ProfileSelectionWeightAdversarial, SameKeyAlwaysPicksSameProfile) {
  auto w = default_profile_weights();
  RuntimePlatformHints platform{DeviceClass::Desktop, MobileOs::None, DesktopOs::Unknown};
  auto allowed = allowed_profiles_for_platform(platform);

  SelectionKey key;
  key.destination = "sticky.example.com";
  key.time_bucket = 1111;

  MockRng rng0(1);
  auto first = pick_profile_sticky(w, key, platform, allowed, rng0);

  for (int i = 0; i < 64; i++) {
    MockRng rng(static_cast<td::uint64>(i) + 9);
    auto next = pick_profile_sticky(w, key, platform, allowed, rng);
    ASSERT_TRUE(next == first);
  }
}

TEST(ProfileSelectionWeightAdversarial, AdjacentBucketsProduceAtLeastTwoProfilesOverTime) {
  auto w = default_profile_weights();
  RuntimePlatformHints platform{DeviceClass::Desktop, MobileOs::None, DesktopOs::Unknown};
  auto allowed = allowed_profiles_for_platform(platform);
  ASSERT_TRUE(allowed.size() >= 2u);

  SelectionKey key;
  key.destination = "rotation.example.com";
  std::set<BrowserProfile> seen;

  for (td::uint32 bucket = 0; bucket < 500; bucket++) {
    key.time_bucket = bucket;
    MockRng rng(static_cast<td::uint64>(bucket) + 101);
    seen.insert(pick_profile_sticky(w, key, platform, allowed, rng));
  }

  std::fprintf(stderr, "[ProfileSelectionWeightAdversarial] seen=%zu\n", seen.size());
  ASSERT_TRUE(seen.size() >= 2u);
}

TEST(ProfileSelectionWeightAdversarial, HighWeightsTotalStillFitsUint32) {
  ProfileWeights w;
  w.chrome133 = 255;
  w.chrome131 = 255;
  w.chrome120 = 255;
  w.firefox148 = 255;
  w.safari26_3 = 255;
  w.ios14 = 255;
  w.chromium_macos_no_alps = 255;
  w.chromium_macos_4469 = 255;
  w.chromium_macos_44cd = 255;
  w.firefox149_android = 255;
  w.android11_okhttp_advisory = 255;
  w.chrome147_windows = 255;
  w.chrome147_ios_chromium = 255;
  w.firefox149_macos26_3 = 255;
  w.firefox149_windows = 255;
  w.android_chromium_alps = 255;

  td::uint64 total = 0;
  for (auto p : all_profiles()) {
    total += profile_weight_of(w, p);
  }
  ASSERT_TRUE(total <= static_cast<td::uint64>(std::numeric_limits<td::uint32>::max()));
}

TEST(ProfileSelectionWeightAdversarial, SelectionKeyHandlesExtremeTimestamps) {
  const td::int32 ts_values[] = {0, 1, -1, std::numeric_limits<td::int32>::max(),
                                 std::numeric_limits<td::int32>::min()};
  for (auto ts : ts_values) {
    auto key = make_profile_selection_key(td::Slice("example.com"), ts);
    ASSERT_TRUE(key.destination == "example.com");
    ASSERT_TRUE(key.time_bucket <= std::numeric_limits<td::uint32>::max());
  }
}

TEST(ProfileSelectionWeightAdversarial, LongAndOddDestinationsDoNotCrashSelection) {
  const td::string destinations[] = {
      "",
      "a",
      "com",
      "verylongdomainnamethatexceeds64bytecharacterlimitforasingleDNSlabel.example.com",
      "xn--0zwm56d.example.com",
      "192.168.1.1",
      "::1",
  };

  auto w = default_profile_weights();
  RuntimePlatformHints platform{DeviceClass::Desktop, MobileOs::None, DesktopOs::Unknown};
  auto allowed = allowed_profiles_for_platform(platform);

  for (size_t i = 0; i < sizeof(destinations) / sizeof(destinations[0]); i++) {
    SelectionKey key = make_profile_selection_key(td::Slice(destinations[i]), 1712345678);
    MockRng rng(static_cast<td::uint64>(i) + 77);
    auto picked = pick_profile_sticky(w, key, platform, allowed, rng);
    bool valid = false;
    for (auto p : allowed) {
      if (p == picked) {
        valid = true;
        break;
      }
    }
    ASSERT_TRUE(valid);
  }
}

}  // namespace
