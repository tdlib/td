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

#include <set>

namespace {

using namespace td;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;
using namespace td::mtproto::test::fixtures;
using namespace td::mtproto::test::fixtures::reviewed;

constexpr uint64 kCorpusIterations = kQuickIterations;
constexpr int32 kUnixTime = 1712345678;

ParsedClientHello build_android_no_alps(uint64 seed) {
  MockRng rng(corpus_seed_for_iteration(seed, kCorpusIterations));
  auto parsed = parse_tls_client_hello(
      build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                         BrowserProfile::Android11_OkHttp_Advisory, EchMode::Disabled, rng));
  CHECK(parsed.is_ok());
  return parsed.move_as_ok();
}

TEST(AndroidChromiumNoAlpsCorpus1k, AdvisoryProfileNeverAdvertisesAlpsPqEchOrPsk) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = build_android_no_alps(seed);
    auto extensions = extension_set_non_grease_no_padding(hello);
    ASSERT_TRUE(extensions.count(kAlpsChrome131) == 0);
    ASSERT_TRUE(extensions.count(kAlpsChrome133Plus) == 0);
    ASSERT_TRUE(extensions.count(kEchExtensionType) == 0);
    ASSERT_TRUE(extensions.count(0x0029u) == 0);
    ASSERT_TRUE(std::find(hello.supported_groups.begin(), hello.supported_groups.end(), kPqHybridGroup) ==
                hello.supported_groups.end());
  }
}

TEST(AndroidChromiumNoAlpsCorpus1k, AdvisoryProfileExtensionOrderIsFixedAcrossSeeds) {
  std::set<string> orderings;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    string key;
    for (auto ext : non_grease_extension_sequence(build_android_no_alps(seed))) {
      if (!key.empty()) {
        key += ",";
      }
      key += hex_u16(ext);
    }
    orderings.insert(key);
  }
  ASSERT_EQ(1u, orderings.size());
}

TEST(AndroidChromiumNoAlpsCorpus1k, AdvisoryProfileKeepsGreaseAnchorsWhileRemainingNoAlps) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = build_android_no_alps(seed);
    auto cipher_suites = parse_cipher_suite_vector(hello.cipher_suites).move_as_ok();
    ASSERT_FALSE(cipher_suites.empty());
    ASSERT_TRUE(is_grease_value(cipher_suites.front()));
    ASSERT_FALSE(hello.extensions.empty());
    ASSERT_TRUE(is_grease_value(hello.extensions.front().type));
    ASSERT_TRUE(is_grease_value(hello.extensions.back().type));
  }
}

TEST(AndroidChromiumNoAlpsCorpus1k, AdvisoryProfileStaysDistinctFromDesktopChromeAndAndroidAlpsFamilies) {
  auto android_alps_fixture = make_unordered_set(yandex26_3_4_android16_samsungNonGreaseExtensionsWithoutPadding);
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto extensions = extension_set_non_grease_no_padding(build_android_no_alps(seed));
    ASSERT_TRUE(extensions != kChrome133EchExtensionSet);
    ASSERT_TRUE(extensions != android_alps_fixture);
    ASSERT_TRUE(extensions.count(kAlpsChrome133Plus) == 0);
  }
}

TEST(AndroidChromiumNoAlpsCorpus1k, YandexSamsungAndOneplusFixturesRemainDifferentFamilies) {
  auto yandex_samsung = make_unordered_set(yandex26_3_4_android16_samsungNonGreaseExtensionsWithoutPadding);
  auto yandex_oneplus = make_unordered_set(yandex26_3_4_128_oxygenos16_oneplus13NonGreaseExtensionsWithoutPadding);
  ASSERT_TRUE(yandex_samsung != yandex_oneplus);
  ASSERT_TRUE(yandex_samsung.count(kAlpsChrome133Plus) != 0);
  ASSERT_TRUE(yandex_oneplus.count(kAlpsChrome133Plus) == 0);
  ASSERT_EQ(17u, yandex_samsung.size());
  ASSERT_EQ(15u, yandex_oneplus.size());
}

TEST(AndroidChromiumNoAlpsCorpus1k, AdvisoryProfileDoesNotCollapseIntoIosAppleTlsFixtureFamily) {
  auto ios_apple_tls = make_unordered_set(chrome147_0_7727_47_ios26_4_aNonGreaseExtensionsWithoutPadding);
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    ASSERT_TRUE(extension_set_non_grease_no_padding(build_android_no_alps(seed)) != ios_apple_tls);
  }
}

}  // namespace