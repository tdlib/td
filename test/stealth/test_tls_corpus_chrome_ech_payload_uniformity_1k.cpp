// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/CorpusStatHelpers.h"
#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/Interfaces.h"
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
const uint32 kMinimumPayloadCoverage = static_cast<uint32>(kCorpusIterations / 8);
const uint32 kHalfRunThreshold = static_cast<uint32>(kCorpusIterations / 2);

class FixedValueRng final : public IRng {
 public:
  explicit FixedValueRng(uint32 value) : value_(value) {
  }

  void fill_secure_bytes(MutableSlice dest) final {
    for (size_t i = 0; i < dest.size(); i++) {
      dest[i] = static_cast<char>((value_ >> ((i % 4) * 8)) & 0xFF);
    }
  }

  uint32 secure_uint32() final {
    return value_;
  }

  uint32 bounded(uint32 n) final {
    CHECK(n != 0);
    return value_ % n;
  }

 private:
  uint32 value_;
};

ParsedClientHello build_ech_hello(BrowserProfile profile, uint64 seed) {
  MockRng rng(corpus_seed_for_iteration(seed, kCorpusIterations));
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile,
                                                 EchMode::Rfc9180Outer, rng);
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  return parsed.move_as_ok();
}

uint16 build_fixed_rng_ech_payload_length(BrowserProfile profile, uint32 fixed_value) {
  FixedValueRng rng(fixed_value);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile,
                                                 EchMode::Rfc9180Outer, rng);
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  return parsed.ok().ech_payload_length;
}

TEST(ChromeEchPayloadUniformity1k, EchPayloadLengthAllFourValuesAppear) {
  FrequencyCounter<uint16> counter;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    counter.count(build_ech_hello(BrowserProfile::Chrome133, seed).ech_payload_length);
  }
  ASSERT_EQ(4u, counter.distinct_values());
}

TEST(ChromeEchPayloadUniformity1k, EchPayloadLengthEachValueMeetsMinimumCoverage) {
  FrequencyCounter<uint16> counter;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    counter.count(build_ech_hello(BrowserProfile::Chrome133, seed).ech_payload_length);
  }
  for (auto payload_length : {144u, 176u, 208u, 240u}) {
    ASSERT_TRUE(counter.observed(static_cast<uint16>(payload_length)) >= kMinimumPayloadCoverage);
  }
}

TEST(ChromeEchPayloadUniformity1k, EchPayloadLength144DoesNotDominate) {
  FrequencyCounter<uint16> counter;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    counter.count(build_ech_hello(BrowserProfile::Chrome133, seed).ech_payload_length);
  }
  ASSERT_TRUE(counter.observed(144) <= kHalfRunThreshold);
}

TEST(ChromeEchPayloadUniformity1k, EchPayloadLengthNoValueExceedsHalfTheRuns) {
  FrequencyCounter<uint16> counter;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    counter.count(build_ech_hello(BrowserProfile::Chrome133, seed).ech_payload_length);
  }
  ASSERT_TRUE(counter.max_observed() <= kHalfRunThreshold);
}

TEST(ChromeEchPayloadUniformity1k, EchPayloadLengthDistributionApproximatelyUniform) {
  FrequencyCounter<uint16> counter;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    counter.count(build_ech_hello(BrowserProfile::Chrome133, seed).ech_payload_length);
  }
  ASSERT_TRUE(static_cast<double>(counter.max_observed()) / static_cast<double>(counter.min_observed()) < 3.0);
}

TEST(ChromeEchPayloadUniformity1k, EchPayloadLengthOnlyAllowedValuesObserved) {
  auto allowed = std::unordered_set<uint16>{144, 176, 208, 240};
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = build_ech_hello(BrowserProfile::Chrome133, seed);
    ASSERT_TRUE(allowed.count(hello.ech_payload_length) != 0);
  }
}

TEST(ChromeEchPayloadUniformity1k, EchPayloadLengthMatchesWireExtensionBody) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    MockRng rng(corpus_seed_for_iteration(seed, kCorpusIterations));
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    auto hello = parsed.move_as_ok();
    auto *ech = find_extension(hello, fixtures::kEchExtensionType);
    ASSERT_TRUE(ech != nullptr);
    auto expected_wire_size =
        static_cast<size_t>(1 + 2 + 2 + 1 + 2 + hello.ech_actual_enc_length + 2 + hello.ech_payload_length);
    ASSERT_EQ(expected_wire_size, ech->value.size());
  }
}

TEST(ChromeEchPayloadUniformity1k, Chrome131EchPayloadLengthHasAllFourValues) {
  FrequencyCounter<uint16> counter;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    counter.count(build_ech_hello(BrowserProfile::Chrome131, seed).ech_payload_length);
  }
  ASSERT_EQ(4u, counter.distinct_values());
  for (auto payload_length : {144u, 176u, 208u, 240u}) {
    ASSERT_TRUE(counter.observed(static_cast<uint16>(payload_length)) >= kMinimumPayloadCoverage);
  }
}

TEST(ChromeEchPayloadUniformity1k, EchEncKeyAlwaysX25519Length) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = build_ech_hello(BrowserProfile::Chrome133, seed);
    ASSERT_EQ(32u, hello.ech_actual_enc_length);
  }
}

TEST(ChromeEchPayloadUniformity1k, EchDisabledLaneHasNoEchExtension) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    MockRng rng(corpus_seed_for_iteration(seed, kCorpusIterations));
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(find_extension(parsed.ok(), fixtures::kEchExtensionType) == nullptr);
  }
}

TEST(ChromeEchPayloadUniformity1k, EchPayloadLengthWithAllOnesRngStaysInAllowedRange) {
  auto payload_length = build_fixed_rng_ech_payload_length(BrowserProfile::Chrome133, 0xFFFFFFFFu);
  ASSERT_EQ(240u, payload_length);
}

TEST(ChromeEchPayloadUniformity1k, EchPayloadLengthWithAllZerosRngStaysInAllowedRange) {
  auto payload_length = build_fixed_rng_ech_payload_length(BrowserProfile::Chrome133, 0u);
  ASSERT_EQ(144u, payload_length);
}

}  // namespace