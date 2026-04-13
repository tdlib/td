// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/CorpusIterationTiers.h"
#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"

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

ParsedClientHello build_profile_hello(BrowserProfile profile, EchMode ech_mode, uint64 seed) {
  MockRng rng(corpus_seed_for_iteration(seed, kCorpusIterations));
  auto wire =
      build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile, ech_mode, rng);
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  return parsed.move_as_ok();
}

void assert_exact_alps_type(BrowserProfile profile, EchMode ech_mode, uint16 expected_type) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = build_profile_hello(profile, ech_mode, seed);
    ASSERT_TRUE(find_extension(hello, expected_type) != nullptr);
    ASSERT_TRUE(find_extension(hello, expected_type == kAlpsChrome131 ? kAlpsChrome133Plus : kAlpsChrome131) ==
                nullptr);
  }
}

void assert_no_alps_types(BrowserProfile profile, EchMode ech_mode) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = build_profile_hello(profile, ech_mode, seed);
    ASSERT_TRUE(find_extension(hello, kAlpsChrome131) == nullptr);
    ASSERT_TRUE(find_extension(hello, kAlpsChrome133Plus) == nullptr);
  }
}

TEST(AlpsTypeConsistency1k, Chrome133EchAlwaysUses44CDOnly) {
  assert_exact_alps_type(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, kAlpsChrome133Plus);
}

TEST(AlpsTypeConsistency1k, Chrome133NoEchAlwaysUses44CDOnly) {
  assert_exact_alps_type(BrowserProfile::Chrome133, EchMode::Disabled, kAlpsChrome133Plus);
}

TEST(AlpsTypeConsistency1k, Chrome131EchAlwaysUses4469Only) {
  assert_exact_alps_type(BrowserProfile::Chrome131, EchMode::Rfc9180Outer, kAlpsChrome131);
}

TEST(AlpsTypeConsistency1k, Chrome131NoEchAlwaysUses4469Only) {
  assert_exact_alps_type(BrowserProfile::Chrome131, EchMode::Disabled, kAlpsChrome131);
}

TEST(AlpsTypeConsistency1k, Chrome120AlwaysUses4469Only) {
  assert_exact_alps_type(BrowserProfile::Chrome120, EchMode::Disabled, kAlpsChrome131);
}

TEST(AlpsTypeConsistency1k, FirefoxNeverUsesAnyAlpsCodepoint) {
  assert_no_alps_types(BrowserProfile::Firefox148, EchMode::Rfc9180Outer);
}

TEST(AlpsTypeConsistency1k, SafariNeverUsesAnyAlpsCodepoint) {
  assert_no_alps_types(BrowserProfile::Safari26_3, EchMode::Disabled);
}

TEST(AlpsTypeConsistency1k, IosAppleTlsNeverUsesAnyAlpsCodepoint) {
  assert_no_alps_types(BrowserProfile::IOS14, EchMode::Disabled);
}

TEST(AlpsTypeConsistency1k, AndroidAdvisoryNeverUsesAnyAlpsCodepoint) {
  assert_no_alps_types(BrowserProfile::Android11_OkHttp_Advisory, EchMode::Disabled);
}

TEST(AlpsTypeConsistency1k, Chrome133AlpsBodyMatchesExpectedH2Payload) {
  static const char kExpectedBody[] = "\x00\x03\x02\x68\x32";
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = build_profile_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, seed);
    auto *alps = find_extension(hello, kAlpsChrome133Plus);
    ASSERT_TRUE(alps != nullptr);
    ASSERT_EQ(Slice(kExpectedBody, sizeof(kExpectedBody) - 1), alps->value);
  }
}

TEST(AlpsTypeConsistency1k, Chrome131AlpsBodyMatchesExpectedH2Payload) {
  static const char kExpectedBody[] = "\x00\x03\x02\x68\x32";
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = build_profile_hello(BrowserProfile::Chrome131, EchMode::Rfc9180Outer, seed);
    auto *alps = find_extension(hello, kAlpsChrome131);
    ASSERT_TRUE(alps != nullptr);
    ASSERT_EQ(Slice(kExpectedBody, sizeof(kExpectedBody) - 1), alps->value);
  }
}

}  // namespace