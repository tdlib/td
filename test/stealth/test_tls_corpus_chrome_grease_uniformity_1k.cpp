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

const uint64 kCorpusIterations = spot_or_full_corpus_iterations();
constexpr int32 kUnixTime = 1712345678;
const uint32 kDominanceThreshold = static_cast<uint32>(kCorpusIterations / 2);
const uint32 kMostFrequentThreshold = static_cast<uint32>(kCorpusIterations * 3 / 10);

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
  auto *supported_versions = find_extension(hello, 0x002B);
  CHECK(supported_versions != nullptr);
  CHECK(supported_versions->value.size() >= 3);
  return static_cast<uint16>((static_cast<uint8>(supported_versions->value[1]) << 8) |
                             static_cast<uint8>(supported_versions->value[2]));
}

uint32 most_frequent_count(const FrequencyCounter<uint16> &counter) {
  return counter.max_observed();
}

TEST(ChromeGreaseUniformity1k, CipherSuiteSlot0GreaseIsDistributedAcrossMultipleValues) {
  FrequencyCounter<uint16> counter;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    counter.count(first_cipher_grease(build_chrome133_hello(seed)));
  }
  ASSERT_TRUE(counter.distinct_values() >= 8u);
}

TEST(ChromeGreaseUniformity1k, SupportedGroupsSlot0GreaseIsDistributedAcrossMultipleValues) {
  FrequencyCounter<uint16> counter;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    counter.count(build_chrome133_hello(seed).supported_groups.front());
  }
  ASSERT_TRUE(counter.distinct_values() >= 8u);
}

TEST(ChromeGreaseUniformity1k, ExtensionSlot0GreaseIsDistributedAcrossMultipleValues) {
  FrequencyCounter<uint16> counter;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    counter.count(first_extension_grease(build_chrome133_hello(seed)));
  }
  ASSERT_TRUE(counter.distinct_values() >= 8u);
}

TEST(ChromeGreaseUniformity1k, ExtensionLastGreaseIsDistributedAcrossMultipleValues) {
  FrequencyCounter<uint16> counter;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    counter.count(last_extension_grease(build_chrome133_hello(seed)));
  }
  ASSERT_TRUE(counter.distinct_values() >= 8u);
}

TEST(ChromeGreaseUniformity1k, KeyShareSlot0GreaseGroupVariesAcross1024Seeds) {
  FrequencyCounter<uint16> counter;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    counter.count(build_chrome133_hello(seed).key_share_entries.front().group);
  }
  ASSERT_TRUE(counter.distinct_values() >= 8u);
}

TEST(ChromeGreaseUniformity1k, SupportedVersionsGreaseVariesAcross1024Seeds) {
  FrequencyCounter<uint16> counter;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    counter.count(supported_versions_grease(build_chrome133_hello(seed)));
  }
  ASSERT_TRUE(counter.distinct_values() >= 4u);
}

TEST(ChromeGreaseUniformity1k, NoCipherGreaseValueExceedsHalfOfAllRuns) {
  FrequencyCounter<uint16> counter;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    counter.count(first_cipher_grease(build_chrome133_hello(seed)));
  }
  ASSERT_TRUE(most_frequent_count(counter) < kDominanceThreshold);
}

TEST(ChromeGreaseUniformity1k, NoCipherGreaseIsZeroOrAllFF) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto value = first_cipher_grease(build_chrome133_hello(seed));
    ASSERT_NE(0u, value);
    ASSERT_NE(0xFFFFu, value);
    ASSERT_TRUE(is_grease_value(value));
  }
}

TEST(ChromeGreaseUniformity1k, GreaseCipherSlot0MostFrequentValueUnderThreshold) {
  FrequencyCounter<uint16> counter;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    counter.count(first_cipher_grease(build_chrome133_hello(seed)));
  }
  ASSERT_TRUE(most_frequent_count(counter) < kMostFrequentThreshold);
}

TEST(ChromeGreaseUniformity1k, GreaseSupportedGroupsSlot0MostFrequentValueUnderThreshold) {
  FrequencyCounter<uint16> counter;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    counter.count(build_chrome133_hello(seed).supported_groups.front());
  }
  ASSERT_TRUE(most_frequent_count(counter) < kMostFrequentThreshold);
}

}  // namespace