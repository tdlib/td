// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/CorpusIterationTiers.h"
#include "test/stealth/CorpusStatHelpers.h"

#include "td/utils/tests.h"

namespace {

using namespace td;
using namespace td::mtproto::test;
using namespace td::mtproto::test::fixtures::reviewed;

std::unordered_set<uint16> without_type(std::unordered_set<uint16> values, uint16 type) {
  values.erase(type);
  return values;
}

std::unordered_set<uint16> symmetric_difference(const std::unordered_set<uint16> &lhs,
                                                const std::unordered_set<uint16> &rhs) {
  std::unordered_set<uint16> result;
  for (auto value : lhs) {
    if (rhs.count(value) == 0) {
      result.insert(value);
    }
  }
  for (auto value : rhs) {
    if (lhs.count(value) == 0) {
      result.insert(value);
    }
  }
  return result;
}

TEST(CrossPlatformContamination1k, LinuxDesktopChromeNeverMatchesAppleTlsSet) {
  auto apple_tls = make_unordered_set(chrome147_0_7727_47_ios26_4_aNonGreaseExtensionsWithoutPadding);
  ASSERT_TRUE(kChrome133EchExtensionSet != apple_tls);
  ASSERT_EQ(13u, apple_tls.size());
  ASSERT_TRUE(kChrome133EchExtensionSet.count(0x44CD) != 0);
  ASSERT_TRUE(apple_tls.count(0x44CD) == 0);
}

TEST(CrossPlatformContamination1k, LinuxDesktopFirefoxNeverMatchesAppleTlsSet) {
  auto firefox_linux = make_unordered_set(firefox148_linux_desktopNonGreaseExtensionsWithoutPadding);
  auto apple_tls = make_unordered_set(chrome147_0_7727_47_ios26_4_aNonGreaseExtensionsWithoutPadding);
  ASSERT_TRUE(firefox_linux != apple_tls);
  ASSERT_EQ(17u, firefox_linux.size());
  ASSERT_EQ(13u, apple_tls.size());
}

TEST(CrossPlatformContamination1k, IosAppleTlsNeverMatchesAndroidChromiumNoAlpsSet) {
  auto apple_tls = make_unordered_set(chrome147_0_7727_47_ios26_4_aNonGreaseExtensionsWithoutPadding);
  auto android_no_alps = make_unordered_set(chrome146_177_android16NonGreaseExtensionsWithoutPadding);
  ASSERT_TRUE(apple_tls != android_no_alps);
  ASSERT_TRUE(apple_tls.count(fixtures::kEchExtensionType) == 0);
  ASSERT_TRUE(android_no_alps.count(fixtures::kEchExtensionType) != 0);
}

TEST(CrossPlatformContamination1k, AndroidYandexDeviceFamiliesRemainDistinct) {
  auto yandex_samsung = make_unordered_set(yandex26_3_4_android16_samsungNonGreaseExtensionsWithoutPadding);
  auto yandex_oneplus = make_unordered_set(yandex26_3_4_128_oxygenos16_oneplus13NonGreaseExtensionsWithoutPadding);
  ASSERT_TRUE(yandex_samsung != yandex_oneplus);
  ASSERT_TRUE(yandex_samsung.count(0x44CD) != 0);
  ASSERT_TRUE(yandex_oneplus.count(0x44CD) == 0);
  ASSERT_EQ(17u, yandex_samsung.size());
  ASSERT_EQ(15u, yandex_oneplus.size());
}

TEST(CrossPlatformContamination1k, ChromeIos261AndIos264FamiliesRetainAlpsSplit) {
  auto ios_261 = make_unordered_set(chrome146_0_7680_151_ios26_1NonGreaseExtensionsWithoutPadding);
  auto ios_264 = make_unordered_set(chrome147_0_7727_47_ios26_4_aNonGreaseExtensionsWithoutPadding);
  ASSERT_TRUE(ios_261.count(0x44CD) != 0);
  ASSERT_TRUE(ios_264.count(0x44CD) == 0);
  ASSERT_TRUE(ios_261.count(fixtures::kEchExtensionType) != 0);
  ASSERT_TRUE(ios_264.count(fixtures::kEchExtensionType) == 0);
}

TEST(CrossPlatformContamination1k, MacosFirefoxAndLinuxFirefoxDifferOnlyBySessionTicketWhenIgnoringPsk) {
  auto firefox_linux =
      without_type(make_unordered_set(firefox149_linux_desktopNonGreaseExtensionsWithoutPadding), 0x0029);
  auto firefox_macos = without_type(make_unordered_set(firefox149_macos26_3NonGreaseExtensionsWithoutPadding), 0x0029);
  auto diff = symmetric_difference(firefox_linux, firefox_macos);
  ASSERT_EQ(1u, diff.size());
  ASSERT_TRUE(diff.count(0x0023) != 0);
}

TEST(CrossPlatformContamination1k, MacosFirefoxEchPayloadRemainsDistinctFromLinuxFirefox) {
  ASSERT_EQ(239u, firefox149_linux_desktopEch.payload_length);
  ASSERT_EQ(399u, firefox149_macos26_3Ech.payload_length);
}

}  // namespace