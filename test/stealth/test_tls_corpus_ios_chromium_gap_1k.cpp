// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/CorpusStatHelpers.h"
#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using namespace td;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;
using namespace td::mtproto::test::fixtures;
using namespace td::mtproto::test::fixtures::reviewed;

constexpr uint64 kCorpusIterations = 1024;
constexpr int32 kUnixTime = 1712345678;

ParsedClientHello build_profile_hello(BrowserProfile profile, EchMode ech_mode, uint64 seed) {
  MockRng rng(seed);
  auto parsed = parse_tls_client_hello(
      build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile, ech_mode, rng));
  CHECK(parsed.is_ok());
  return parsed.move_as_ok();
}

TEST(IosChromiumGap1k, Ios14ProfileDoesNotMatchChromium261ExtensionSet) {
  auto ios_chromium = make_unordered_set(chrome146_0_7680_151_ios26_1NonGreaseExtensionsWithoutPadding);
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    ASSERT_TRUE(extension_set_non_grease_no_padding(build_profile_hello(BrowserProfile::IOS14, EchMode::Disabled, seed)) !=
                ios_chromium);
  }
}

TEST(IosChromiumGap1k, Ios14ProfileNeverAdvertisesAlpsWhichChromiumFamilyHas) {
  auto ios_264_apple_tls = make_unordered_set(chrome147_0_7727_47_ios26_4_aNonGreaseExtensionsWithoutPadding);
  auto ios_261_chromium = make_unordered_set(chrome146_0_7680_151_ios26_1NonGreaseExtensionsWithoutPadding);
  ASSERT_TRUE(ios_264_apple_tls.count(kAlpsChrome133Plus) == 0);
  ASSERT_TRUE(ios_261_chromium.count(kAlpsChrome133Plus) != 0);

  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto extensions = extension_set_non_grease_no_padding(build_profile_hello(BrowserProfile::IOS14, EchMode::Disabled, seed));
    ASSERT_TRUE(extensions.count(kAlpsChrome133Plus) == 0);
  }
}

TEST(IosChromiumGap1k, Chrome133NonRuProfileDoesNotCollapseIntoIosChromiumFamily) {
  auto ios_chromium = make_unordered_set(chrome147_0_7727_47_ios26_3NonGreaseExtensionsWithoutPadding);
  ASSERT_TRUE(ios_chromium.count(0x0029u) != 0);

  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = build_profile_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, seed);
    auto extensions = extension_set_non_grease_no_padding(hello);
    ASSERT_TRUE(extensions != ios_chromium);
    ASSERT_TRUE(extensions.count(0x0029u) == 0);
    ASSERT_TRUE(find_extension(hello, 0x0010u) != nullptr);
  }
}

TEST(IosChromiumGap1k, FixtureRegressionKeepsIos261AndIos264FamiliesDistinct) {
  auto ios_261 = make_unordered_set(chrome146_0_7680_151_ios26_1NonGreaseExtensionsWithoutPadding);
  auto ios_263 = make_unordered_set(chrome147_0_7727_47_ios26_3NonGreaseExtensionsWithoutPadding);
  auto ios_264 = make_unordered_set(chrome147_0_7727_47_ios26_4_aNonGreaseExtensionsWithoutPadding);

  ASSERT_EQ(17u, ios_261.size());
  ASSERT_EQ(17u, ios_263.size());
  ASSERT_EQ(13u, ios_264.size());
  ASSERT_TRUE(ios_261.count(kAlpsChrome133Plus) != 0);
  ASSERT_TRUE(ios_263.count(kAlpsChrome133Plus) != 0);
  ASSERT_TRUE(ios_264.count(kAlpsChrome133Plus) == 0);
  ASSERT_TRUE(ios_261.count(kEchExtensionType) != 0);
  ASSERT_TRUE(ios_263.count(kEchExtensionType) != 0);
  ASSERT_TRUE(ios_264.count(kEchExtensionType) == 0);
}

}  // namespace