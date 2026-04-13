// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/CorpusIterationTiers.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/TestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <unordered_set>

namespace {

using namespace td;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;

const uint64 kCorpusIterations = spot_or_full_corpus_iterations();
const size_t kGreasePairCoverageFloor = is_nightly_corpus_enabled() ? 64u : 32u;
constexpr int32 kUnixTime = 1712345678;

ParsedClientHello build_chrome133_hello(uint64 seed) {
  MockRng rng(corpus_seed_for_iteration(seed, kCorpusIterations));
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                 BrowserProfile::Chrome133, EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  return parsed.move_as_ok();
}

uint16 first_extension_grease(const ParsedClientHello &hello) {
  for (const auto &ext : hello.extensions) {
    if (is_grease_value(ext.type)) {
      return ext.type;
    }
  }
  UNREACHABLE();
  return 0;
}

uint16 last_extension_grease(const ParsedClientHello &hello) {
  for (auto it = hello.extensions.rbegin(); it != hello.extensions.rend(); ++it) {
    if (is_grease_value(it->type)) {
      return it->type;
    }
  }
  UNREACHABLE();
  return 0;
}

uint16 first_cipher_grease(const ParsedClientHello &hello) {
  auto cipher_suites = parse_cipher_suite_vector(hello.cipher_suites).move_as_ok();
  for (auto cipher_suite : cipher_suites) {
    if (is_grease_value(cipher_suite)) {
      return cipher_suite;
    }
  }
  UNREACHABLE();
  return 0;
}

uint16 supported_versions_grease(const ParsedClientHello &hello) {
  auto *supported_versions = find_extension(hello, 0x002Bu);
  CHECK(supported_versions != nullptr);
  CHECK(supported_versions->value.size() >= 3);
  return static_cast<uint16>((static_cast<uint8>(supported_versions->value[1]) << 8) |
                             static_cast<uint8>(supported_versions->value[2]));
}

double equality_rate(size_t equal_count, size_t total_count) {
  CHECK(total_count != 0u);
  return static_cast<double>(equal_count) / static_cast<double>(total_count);
}

TEST(GreaseSlotIndependence1k, FirstCipherAndFirstExtensionGreaseDoNotMatchTooOften) {
  size_t equal_count = 0;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = build_chrome133_hello(seed);
    if (first_cipher_grease(hello) == first_extension_grease(hello)) {
      equal_count++;
    }
  }
  ASSERT_TRUE(equality_rate(equal_count, kCorpusIterations) < 0.25);
}

TEST(GreaseSlotIndependence1k, FirstAndLastExtensionGreaseDoNotMatchTooOften) {
  size_t equal_count = 0;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = build_chrome133_hello(seed);
    if (first_extension_grease(hello) == last_extension_grease(hello)) {
      equal_count++;
    }
  }
  ASSERT_TRUE(equality_rate(equal_count, kCorpusIterations) < 0.25);
}

TEST(GreaseSlotIndependence1k, FirstExtensionAndSupportedVersionsGreaseDoNotMatchTooOften) {
  size_t equal_count = 0;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = build_chrome133_hello(seed);
    if (first_extension_grease(hello) == supported_versions_grease(hello)) {
      equal_count++;
    }
  }
  ASSERT_TRUE(equality_rate(equal_count, kCorpusIterations) < 0.25);
}

TEST(GreaseSlotIndependence1k, FirstCipherAndKeyShareGreaseDoNotMatchTooOften) {
  size_t equal_count = 0;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = build_chrome133_hello(seed);
    ASSERT_FALSE(hello.key_share_entries.empty());
    if (first_cipher_grease(hello) == hello.key_share_entries.front().group) {
      equal_count++;
    }
  }
  ASSERT_TRUE(equality_rate(equal_count, kCorpusIterations) < 0.25);
}

TEST(GreaseSlotIndependence1k, GreaseExtensionAnchorsRarelyCollapseToSameValue) {
  size_t equal_count = 0;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = build_chrome133_hello(seed);
    if (first_extension_grease(hello) == last_extension_grease(hello)) {
      equal_count++;
    }
  }
  ASSERT_TRUE(equality_rate(equal_count, kCorpusIterations) < 0.05);
}

TEST(GreaseSlotIndependence1k, GreasePairCoverageShowsManyDistinctPairs) {
  std::unordered_set<uint32> pairs;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = build_chrome133_hello(seed);
    auto pair_key = (static_cast<uint32>(first_cipher_grease(hello)) << 16) | first_extension_grease(hello);
    pairs.insert(pair_key);
  }
  ASSERT_TRUE(pairs.size() >= kGreasePairCoverageFloor);
}

}  // namespace