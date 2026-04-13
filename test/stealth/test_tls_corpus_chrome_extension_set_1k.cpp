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

constexpr uint64 kCorpusIterations = kQuickIterations;
constexpr int32 kUnixTime = 1712345678;

string build_client_hello(BrowserProfile profile, EchMode ech_mode, uint64 seed) {
  MockRng rng(corpus_seed_for_iteration(seed, kCorpusIterations));
  return build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile, ech_mode, rng);
}

ParsedClientHello parse_client_hello_or_die(Slice wire) {
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  return parsed.move_as_ok();
}

void assert_set_eq(const std::unordered_set<uint16> &actual, const std::unordered_set<uint16> &expected) {
  ASSERT_EQ(actual.size(), expected.size());
  for (auto value : expected) {
    ASSERT_TRUE(actual.count(value) != 0);
  }
}

void assert_chrome_extension_set_matches(BrowserProfile profile, EchMode ech_mode,
                                         const std::unordered_set<uint16> &expected) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto wire = build_client_hello(profile, ech_mode, seed);
    auto hello = parse_client_hello_or_die(wire);
    assert_set_eq(extension_set_non_grease_no_padding(hello), expected);
  }
}

TEST(ChromeCorpusExtensionSet1k, Chrome133EchExtensionSetExactMatch) {
  assert_chrome_extension_set_matches(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, kChrome133EchExtensionSet);
}

TEST(ChromeCorpusExtensionSet1k, Chrome133NoEchExtensionSetExactMatch) {
  assert_chrome_extension_set_matches(BrowserProfile::Chrome133, EchMode::Disabled, kChrome133NoEchExtensionSet);
}

TEST(ChromeCorpusExtensionSet1k, Chrome131ExtensionSetContainsAlps4469NotAlps44CD) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_client_hello_or_die(build_client_hello(BrowserProfile::Chrome131, EchMode::Rfc9180Outer, seed));
    auto extensions = extension_set_non_grease_no_padding(hello);
    ASSERT_TRUE(extensions.count(kAlpsChrome131) != 0);
    ASSERT_TRUE(extensions.count(kAlpsChrome133Plus) == 0);
  }
}

TEST(ChromeCorpusExtensionSet1k, Chrome133ExtensionSetContainsAlps44CDNotAlps4469) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_client_hello_or_die(build_client_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, seed));
    auto extensions = extension_set_non_grease_no_padding(hello);
    ASSERT_TRUE(extensions.count(kAlpsChrome133Plus) != 0);
    ASSERT_TRUE(extensions.count(kAlpsChrome131) == 0);
  }
}

TEST(ChromeCorpusExtensionSet1k, Chrome120SupportedGroupsPqAbsent) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_client_hello_or_die(build_client_hello(BrowserProfile::Chrome120, EchMode::Disabled, seed));
    ASSERT_TRUE(std::find(hello.supported_groups.begin(), hello.supported_groups.end(), kPqHybridGroup) ==
                hello.supported_groups.end());
  }
}

TEST(ChromeCorpusExtensionSet1k, PaddingExtensionIsNotCountedInExtensionSet) {
  for (auto profile : {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Chrome120}) {
    for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
      auto hello = parse_client_hello_or_die(build_client_hello(profile, EchMode::Disabled, seed));
      auto extensions = extension_set_non_grease_no_padding(hello);
      ASSERT_TRUE(extensions.count(0x0015) == 0);
    }
  }
}

TEST(ChromeCorpusExtensionSet1k, NoSessionResumptionPskOnFreshConnections) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_client_hello_or_die(build_client_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, seed));
    auto extensions = extension_set_non_grease_no_padding(hello);
    ASSERT_TRUE(extensions.count(0x0023) != 0);
    ASSERT_TRUE(extensions.count(0x0029) == 0);
  }
}

TEST(ChromeCorpusExtensionSet1k, ChromeProfilesNeverAdvertise3DesSuites) {
  for (auto profile : {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Chrome120}) {
    for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
      auto hello = parse_client_hello_or_die(build_client_hello(profile, EchMode::Disabled, seed));
      auto cipher_suites = parse_cipher_suite_vector(hello.cipher_suites).move_as_ok();
      ASSERT_TRUE(std::find(cipher_suites.begin(), cipher_suites.end(), kTlsRsaWith3DesEdeCbcSha) ==
                  cipher_suites.end());
      ASSERT_TRUE(std::find(cipher_suites.begin(), cipher_suites.end(), kTlsEcdheEcdsaWith3DesEdeCbcSha) ==
                  cipher_suites.end());
      ASSERT_TRUE(std::find(cipher_suites.begin(), cipher_suites.end(), kTlsEcdheRsaWith3DesEdeCbcSha) ==
                  cipher_suites.end());
    }
  }
}

TEST(ChromeCorpusExtensionSet1k, Chrome133ExtensionCountMatchesFixture) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto ech_hello =
        parse_client_hello_or_die(build_client_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, seed));
    auto no_ech_hello =
        parse_client_hello_or_die(build_client_hello(BrowserProfile::Chrome133, EchMode::Disabled, seed));
    ASSERT_EQ(16u, extension_set_non_grease_no_padding(ech_hello).size());
    ASSERT_EQ(15u, extension_set_non_grease_no_padding(no_ech_hello).size());
  }
}

TEST(ChromeCorpusExtensionSet1k, DuplicateExtensionTypeNeverAppears) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = parse_client_hello_or_die(build_client_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, seed));
    auto counter = extension_type_counter(hello);
    for (const auto &it : counter.counts()) {
      ASSERT_EQ(1u, it.second);
    }
  }
}

TEST(ChromeCorpusExtensionSet1k, FirefoxOnlyExtensionsNeverAppearInChromeProfiles) {
  for (auto profile : {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Chrome120}) {
    for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
      auto hello = parse_client_hello_or_die(build_client_hello(profile, EchMode::Rfc9180Outer, seed));
      auto extensions = extension_set_non_grease_no_padding(hello);
      ASSERT_TRUE(extensions.count(0x0022) == 0);
      ASSERT_TRUE(extensions.count(0x001C) == 0);
    }
  }
}

}  // namespace