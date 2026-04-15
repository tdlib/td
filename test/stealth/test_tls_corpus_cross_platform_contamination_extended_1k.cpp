// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/CorpusIterationTiers.h"
#include "test/stealth/CorpusStatHelpers.h"
#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <unordered_set>

namespace {

using namespace td;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;
using namespace td::mtproto::test::fixtures;
using namespace td::mtproto::test::fixtures::reviewed;

constexpr uint64 kCorpusIterations = kQuickIterations;
constexpr int32 kUnixTime = 1712345678;

ParsedClientHello build_profile_hello(BrowserProfile profile, EchMode ech_mode, uint64 seed) {
  MockRng rng(corpus_seed_for_iteration(seed, kCorpusIterations));
  auto wire =
      build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile, ech_mode, rng);
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  return parsed.move_as_ok();
}

std::unordered_set<uint16> ext_set_for(BrowserProfile profile, EchMode ech_mode, uint64 seed) {
  return extension_set_non_grease_no_padding(build_profile_hello(profile, ech_mode, seed));
}

void assert_set_eq(const std::unordered_set<uint16> &actual, const std::unordered_set<uint16> &expected) {
  ASSERT_EQ(expected.size(), actual.size());
  for (auto value : expected) {
    ASSERT_TRUE(actual.count(value) != 0);
  }
}

// -- Linux Desktop Chrome never matches iOS Chromium family (Plan Phase 12a test 3) --

TEST(CrossPlatformContaminationExtended1k, LinuxDesktopChromeNeverMatchesIosChromiumFamily) {
  auto ios_chromium_261 = make_unordered_set(chrome146_0_7680_151_ios26_1NonGreaseExtensionsWithoutPadding);
  auto ios_chromium_263 = make_unordered_set(chrome147_0_7727_47_ios26_3NonGreaseExtensionsWithoutPadding);
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto linux_chrome_set = ext_set_for(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, seed);
    // Linux Chrome on fresh connections never has PSK (0x0029), which iOS Chromium captures had
    ASSERT_TRUE(linux_chrome_set.count(0x0029) == 0);
    ASSERT_TRUE(linux_chrome_set != ios_chromium_261);
    ASSERT_TRUE(linux_chrome_set != ios_chromium_263);
  }
}

// -- Linux Desktop Chrome differs from Android no-ALPS Chrome (Plan Phase 12a test 4) --

TEST(CrossPlatformContaminationExtended1k, LinuxDesktopChromeExtensionSetDiffersFromAndroidNoAlpsChrome) {
  auto android_no_alps = make_unordered_set(chrome146_177_android16NonGreaseExtensionsWithoutPadding);
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto linux_chrome = ext_set_for(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, seed);
    ASSERT_TRUE(linux_chrome != android_no_alps);
    // Key discriminant: Linux Chrome has ALPS 0x44CD, Android no-ALPS Chrome doesn't
    ASSERT_TRUE(linux_chrome.count(0x44CD) != 0);
    ASSERT_TRUE(android_no_alps.count(0x44CD) == 0);
  }
}

// -- Linux Desktop Firefox never matches Android no-ALPS (Plan Phase 12a test 5) --

TEST(CrossPlatformContaminationExtended1k, LinuxDesktopFirefoxNeverMatchesAndroidNoAlps) {
  auto firefox_set = make_unordered_set(firefox148_linux_desktopNonGreaseExtensionsWithoutPadding);
  auto android_no_alps = make_unordered_set(chrome146_177_android16NonGreaseExtensionsWithoutPadding);
  // Firefox has 17 extensions, Android no-ALPS has 15
  ASSERT_EQ(17u, firefox_set.size());
  ASSERT_EQ(15u, android_no_alps.size());
  ASSERT_TRUE(firefox_set != android_no_alps);
  // Firefox has 0x0022 (delegated_credentials) and 0x001C (record_size_limit), Android doesn't
  ASSERT_TRUE(firefox_set.count(0x0022) != 0);
  ASSERT_TRUE(android_no_alps.count(0x0022) == 0);
}

// -- iOS Apple TLS never matches Android Chromium ALPS (Plan Phase 12a test 7) --

TEST(CrossPlatformContaminationExtended1k, IosAppleTlsSetNeverMatchesAndroidChromiumAlpsSet) {
  auto apple_tls = make_unordered_set(chrome147_0_7727_47_ios26_4_aNonGreaseExtensionsWithoutPadding);
  auto android_alps = make_unordered_set(brave188_138_android13NonGreaseExtensionsWithoutPadding);
  ASSERT_EQ(13u, apple_tls.size());
  ASSERT_EQ(17u, android_alps.size());
  ASSERT_TRUE(apple_tls != android_alps);
  // Android ALPS has ALPS, ECH, GREASE; Apple TLS has none of those
  ASSERT_TRUE(android_alps.count(0x44CD) != 0);
  ASSERT_TRUE(apple_tls.count(0x44CD) == 0);
}

// -- iOS Apple TLS never matches macOS Firefox (Plan Phase 12a test 8) --

TEST(CrossPlatformContaminationExtended1k, IosAppleTlsSetNeverMatchesMacosFirefoxSet) {
  auto apple_tls = make_unordered_set(chrome147_0_7727_47_ios26_4_aNonGreaseExtensionsWithoutPadding);
  auto macos_firefox = make_unordered_set(firefox149_macos26_3NonGreaseExtensionsWithoutPadding);
  ASSERT_TRUE(apple_tls.size() != macos_firefox.size());
  ASSERT_TRUE(apple_tls != macos_firefox);
}

// -- Android ALPS vs Android no-ALPS at runtime profile level (Plan Phase 12a test 9) --

TEST(CrossPlatformContaminationExtended1k, AndroidChromiumAlpsVsNoAlpsAreDifferentFamilies1024Runs) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto chrome133_set = ext_set_for(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, seed);
    auto android_advisory_set = ext_set_for(BrowserProfile::Android11_OkHttp_Advisory, EchMode::Disabled, seed);
    // Chrome133 (best ALPS proxy) always has 0x44CD; Android11 advisory never does
    ASSERT_TRUE(chrome133_set.count(0x44CD) != 0);
    ASSERT_TRUE(android_advisory_set.count(0x44CD) == 0);
    ASSERT_TRUE(chrome133_set != android_advisory_set);
  }
}

// -- Safari26_3 and IOS14 intentionally collapse to the reviewed Apple TLS family --

TEST(CrossPlatformContaminationExtended1k, Safari26_3AndIos14ShareReviewedAppleTlsFamily) {
  auto safari_fixture = make_unordered_set(safari26_3_1_ios26_3_1_aNonGreaseExtensionsWithoutPadding);
  auto ios_fixture = make_unordered_set(chrome147_0_7727_47_ios26_4_aNonGreaseExtensionsWithoutPadding);

  assert_set_eq(safari_fixture, ios_fixture);
  ASSERT_EQ(safari26_3_1_ios26_3_1_aNonGreaseCipherSuites, chrome147_0_7727_47_ios26_4_aNonGreaseCipherSuites);
  ASSERT_EQ(safari26_3_1_ios26_3_1_aNonGreaseSupportedGroups, chrome147_0_7727_47_ios26_4_aNonGreaseSupportedGroups);

  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto safari_hello = build_profile_hello(BrowserProfile::Safari26_3, EchMode::Disabled, seed);
    auto ios14_hello = build_profile_hello(BrowserProfile::IOS14, EchMode::Disabled, seed);
    assert_set_eq(extension_set_non_grease_no_padding(safari_hello), extension_set_non_grease_no_padding(ios14_hello));
  }
}

// -- ChromeIos261 has ECH, ChromeIos264 does not (Plan Phase 12a test 12) --

TEST(CrossPlatformContaminationExtended1k, ChromeIos261FamilyHasEchButIos264FamilyDoesNot) {
  auto ios_261 = make_unordered_set(chrome146_0_7680_151_ios26_1NonGreaseExtensionsWithoutPadding);
  auto ios_264 = make_unordered_set(chrome147_0_7727_47_ios26_4_aNonGreaseExtensionsWithoutPadding);
  ASSERT_TRUE(ios_261.count(kEchExtensionType) != 0);
  ASSERT_TRUE(ios_264.count(kEchExtensionType) == 0);
}

// -- All generated profiles never equal any fixture from a different platform family --

TEST(CrossPlatformContaminationExtended1k, Chrome133ExtensionSetNeverMatchesAnyIosFixture) {
  std::vector<std::unordered_set<uint16>> ios_fixtures = {
      make_unordered_set(chrome147_0_7727_47_ios26_4_aNonGreaseExtensionsWithoutPadding),
      make_unordered_set(brave188_ios26_4NonGreaseExtensionsWithoutPadding),
      make_unordered_set(safari18_7_6_ios18_7_6NonGreaseExtensionsWithoutPadding),
      make_unordered_set(safari26_2_ios26_2_aNonGreaseExtensionsWithoutPadding),
      make_unordered_set(safari26_4_ios26_4_aNonGreaseExtensionsWithoutPadding),
  };
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto chrome_set = ext_set_for(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, seed);
    for (const auto &ios_set : ios_fixtures) {
      ASSERT_TRUE(chrome_set != ios_set);
    }
  }
}

TEST(CrossPlatformContaminationExtended1k, Firefox148ExtensionSetNeverMatchesAnyAndroidFixture) {
  auto firefox_expected = make_unordered_set(firefox148_linux_desktopNonGreaseExtensionsWithoutPadding);
  std::vector<std::unordered_set<uint16>> android_fixtures = {
      make_unordered_set(chrome146_177_android16NonGreaseExtensionsWithoutPadding),
      make_unordered_set(brave188_138_android13NonGreaseExtensionsWithoutPadding),
      make_unordered_set(samsung_internet29_android16_galaxy_s25plusNonGreaseExtensionsWithoutPadding),
      make_unordered_set(yandex26_3_4_128_oxygenos16_oneplus13NonGreaseExtensionsWithoutPadding),
      make_unordered_set(yandex26_3_4_android16_samsungNonGreaseExtensionsWithoutPadding),
  };
  for (const auto &android_set : android_fixtures) {
    ASSERT_TRUE(firefox_expected != android_set);
  }
}

}  // namespace
