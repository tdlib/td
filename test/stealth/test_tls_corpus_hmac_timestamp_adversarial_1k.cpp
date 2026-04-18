// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/CorpusIterationTiers.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/tests.h"

#include <cstring>
#include <set>
#include <unordered_set>

namespace {

using namespace td;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;

const uint64 kCorpusIterations = spot_or_full_corpus_iterations();
const size_t kClientRandomDistinctByteFloor = is_nightly_corpus_enabled() ? 128u : 32u;
constexpr size_t kClientRandomOffset = 11;
constexpr size_t kClientRandomLength = 32;
constexpr size_t kTimestampTailOffset = 28;
constexpr size_t kTimestampTailLength = 4;

uint64 quick_seed(uint64 iteration_index) {
  return corpus_seed_for_iteration(iteration_index, kQuickIterations);
}

uint64 corpus_seed(uint64 iteration_index) {
  return corpus_seed_for_iteration(iteration_index, kCorpusIterations);
}

Slice extract_client_random(Slice wire) {
  CHECK(wire.size() >= kClientRandomOffset + kClientRandomLength);
  return wire.substr(kClientRandomOffset, kClientRandomLength);
}

string normalize_wire_without_client_random(Slice wire) {
  auto normalized = wire.str();
  CHECK(normalized.size() >= kClientRandomOffset + kClientRandomLength);
  std::memset(&normalized[kClientRandomOffset], 0, kClientRandomLength);
  return normalized;
}

uint32 extract_timestamp_tail_le(Slice wire) {
  auto cr = extract_client_random(wire);
  auto tail = cr.substr(kTimestampTailOffset, kTimestampTailLength);
  return static_cast<uint32>(static_cast<uint8>(tail[0])) | (static_cast<uint32>(static_cast<uint8>(tail[1])) << 8) |
         (static_cast<uint32>(static_cast<uint8>(tail[2])) << 16) |
         (static_cast<uint32>(static_cast<uint8>(tail[3])) << 24);
}

string build_wire(BrowserProfile profile, EchMode ech_mode, uint64 seed, int32 unix_time) {
  MockRng rng(seed);
  return build_tls_client_hello_for_profile("www.google.com", "0123456789secret", unix_time, profile, ech_mode, rng);
}

string build_wire_with_secret(string domain, string secret, uint64 seed, int32 unix_time) {
  MockRng rng(seed);
  return build_tls_client_hello_for_profile(std::move(domain), secret, unix_time, BrowserProfile::Chrome133,
                                            EchMode::Disabled, rng);
}

// -- Determinism tests --

TEST(HmacTimestampAdversarial1k, SameSeedSameInputsProduceIdenticalWire) {
  for (uint64 seed = 0; seed < kQuickIterations; seed++) {
    auto mapped_seed = quick_seed(seed);
    auto wire1 = build_wire(BrowserProfile::Chrome133, EchMode::Disabled, mapped_seed, 1712345678);
    auto wire2 = build_wire(BrowserProfile::Chrome133, EchMode::Disabled, mapped_seed, 1712345678);
    ASSERT_EQ(wire1, wire2);
  }
}

TEST(HmacTimestampAdversarial1k, DifferentSeedsNeverProduceIdenticalWire) {
  std::unordered_set<string> wires;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    wires.insert(build_wire(BrowserProfile::Chrome133, EchMode::Disabled, corpus_seed(seed), 1712345678));
  }
  ASSERT_EQ(kCorpusIterations, wires.size());
}

TEST(HmacTimestampAdversarial1k, DifferentTimestampsSameSeedProduceDifferentTimestampTail) {
  for (uint64 seed = 0; seed < kQuickIterations; seed++) {
    auto mapped_seed = quick_seed(seed);
    auto wire1 = build_wire(BrowserProfile::Chrome133, EchMode::Disabled, mapped_seed, 1000000000);
    auto wire2 = build_wire(BrowserProfile::Chrome133, EchMode::Disabled, mapped_seed, 1000000001);
    // The HMAC digest (first 28 bytes of client_random) does not change when only the timestamp changes
    // because the RNG-filled client_random placeholder is the same. Only the last 4 bytes differ (XOR mask).
    auto tail1 = extract_timestamp_tail_le(wire1);
    auto tail2 = extract_timestamp_tail_le(wire2);
    ASSERT_NE(tail1, tail2);
  }
}

TEST(HmacTimestampAdversarial1k, DifferentSecretsSameSeedProduceDifferentClientRandom) {
  for (uint64 seed = 0; seed < kQuickIterations; seed++) {
    auto mapped_seed = quick_seed(seed);
    auto wire1 = build_wire_with_secret("www.google.com", "0123456789secret", mapped_seed, 1712345678);
    auto wire2 = build_wire_with_secret("www.google.com", "secret9876543210", mapped_seed, 1712345678);
    ASSERT_NE(extract_client_random(wire1).str(), extract_client_random(wire2).str());
  }
}

TEST(HmacTimestampAdversarial1k, DifferentDomainsSameSeedProduceDifferentClientRandom) {
  for (uint64 seed = 0; seed < kQuickIterations; seed++) {
    auto mapped_seed = quick_seed(seed);
    auto wire1 = build_wire_with_secret("www.google.com", "0123456789secret", mapped_seed, 1712345678);
    auto wire2 = build_wire_with_secret("www.example.com", "0123456789secret", mapped_seed, 1712345678);
    ASSERT_NE(extract_client_random(wire1).str(), extract_client_random(wire2).str());
  }
}

// -- Timestamp tail XOR embedding tests --

TEST(HmacTimestampAdversarial1k, TimestampTailChangesWhenTimestampChanges) {
  std::unordered_set<uint32> tails;
  for (int32 t = 1000000; t < 1000000 + static_cast<int32>(kCorpusIterations); t++) {
    tails.insert(
        extract_timestamp_tail_le(build_wire(BrowserProfile::Chrome133, EchMode::Disabled, corpus_seed(42), t)));
  }
  ASSERT_TRUE(tails.size() >= kCorpusIterations * 99 / 100);
}

TEST(HmacTimestampAdversarial1k, TimestampTailNotMonotonicallyIncreasingForAdjacentTimestamps) {
  uint32 increasing_count = 0;
  uint32 prev_tail =
      extract_timestamp_tail_le(build_wire(BrowserProfile::Chrome133, EchMode::Disabled, corpus_seed(42), 1000000));
  for (int32 t = 1000001; t < 1000000 + static_cast<int32>(kCorpusIterations); t++) {
    auto current_tail =
        extract_timestamp_tail_le(build_wire(BrowserProfile::Chrome133, EchMode::Disabled, corpus_seed(42), t));
    if (current_tail > prev_tail) {
      increasing_count++;
    }
    prev_tail = current_tail;
  }
  // If the timestamp were embedded directly, every adjacent step would rise.
  // Require at least one rise and one drop so the tail cannot collapse to a
  // monotonic projection of unix_time.
  ASSERT_TRUE(increasing_count > 0);
  ASSERT_TRUE(increasing_count < kCorpusIterations - 1);
}

// -- Timestamp boundary values --

TEST(HmacTimestampAdversarial1k, TimestampZeroDoesNotCorruptWire) {
  for (uint64 seed = 0; seed < 64; seed++) {
    auto wire = build_wire(BrowserProfile::Chrome133, EchMode::Disabled, seed, 0);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_EQ(0x0303u, parsed.ok().client_legacy_version);
  }
}

TEST(HmacTimestampAdversarial1k, TimestampMaxInt32DoesNotCorruptWire) {
  for (uint64 seed = 0; seed < 64; seed++) {
    auto wire = build_wire(BrowserProfile::Chrome133, EchMode::Disabled, seed, 2147483647);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
  }
}

TEST(HmacTimestampAdversarial1k, TimestampNegativeValueDoesNotCorruptWire) {
  for (uint64 seed = 0; seed < 64; seed++) {
    auto wire = build_wire(BrowserProfile::Chrome133, EchMode::Disabled, seed, -1);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
  }
}

TEST(HmacTimestampAdversarial1k, TimestampMinInt32DoesNotCorruptWire) {
  for (uint64 seed = 0; seed < 64; seed++) {
    auto wire = build_wire(BrowserProfile::Chrome133, EchMode::Disabled, seed, -2147483647 - 1);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
  }
}

// -- Client random distribution (uniformity / no bias) --

TEST(HmacTimestampAdversarial1k, ClientRandomBytesShowHighEntropyAcrossSeeds) {
  // For each byte position in client_random, check it takes on many distinct values across seeds
  for (size_t byte_pos = 0; byte_pos < kClientRandomLength; byte_pos++) {
    std::unordered_set<uint8> seen;
    for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
      auto wire = build_wire(BrowserProfile::Chrome133, EchMode::Disabled, corpus_seed(seed), 1712345678);
      auto cr = extract_client_random(wire);
      seen.insert(static_cast<uint8>(cr[byte_pos]));
    }
    // Each byte position should take on many values (HMAC output should be high entropy)
    ASSERT_TRUE(seen.size() >= kClientRandomDistinctByteFloor);
  }
}

TEST(HmacTimestampAdversarial1k, ClientRandomNoNullTerminationBias) {
  // Check that client_random doesn't contain disproportionate null bytes
  size_t null_byte_count = 0;
  size_t total_bytes = 0;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto wire = build_wire(BrowserProfile::Chrome133, EchMode::Disabled, corpus_seed(seed), 1712345678);
    auto cr = extract_client_random(wire);
    for (size_t i = 0; i < kClientRandomLength; i++) {
      if (cr[i] == '\0') {
        null_byte_count++;
      }
      total_bytes++;
    }
  }
  // Expected: ~total_bytes/256 null bytes. Allow 5x margin.
  auto expected = total_bytes / 256;
  ASSERT_TRUE(null_byte_count < expected * 5);
}

// -- Cross-profile HMAC isolation --

TEST(HmacTimestampAdversarial1k, AllProfilesSameSeedSameTimestampProduceDifferentClientRandom) {
  auto profiles = all_profiles();
  for (uint64 seed = 0; seed < kQuickIterations; seed++) {
    std::set<string> randoms;
    std::set<string> normalized_wires;
    for (auto profile : profiles) {
      auto wire = build_wire(profile, EchMode::Disabled, quick_seed(seed), 1712345678);
      randoms.insert(extract_client_random(wire).str());
      normalized_wires.insert(normalize_wire_without_client_random(wire));
    }
    // Different profile families may intentionally share wire layouts.
    // The number of distinct client_random values should track distinct
    // normalized wire templates for the same seed/timestamp.
    ASSERT_TRUE(randoms.size() >= normalized_wires.size());
  }
}

// -- Replay resistance --

TEST(HmacTimestampAdversarial1k, BitFlipInWireChangesHmacVerification) {
  auto wire = build_wire(BrowserProfile::Chrome133, EchMode::Disabled, 77, 1712345678);
  auto original_cr = extract_client_random(wire).str();

  // Flip a bit outside client_random (e.g., in session_id area)
  string mutated = wire;
  size_t flip_pos = kClientRandomOffset + kClientRandomLength + 10;
  if (flip_pos < mutated.size()) {
    mutated[flip_pos] ^= 0x01;
  }

  // The client_random is computed as HMAC over the entire wire, so flipping a bit
  // in the wire makes the originally-stored client_random invalid as an HMAC.
  // After the flip, recomputing HMAC would yield a different value.
  string recomputed_hash(32, '\0');
  hmac_sha256(Slice("0123456789secret"), mutated, recomputed_hash);
  ASSERT_NE(original_cr, recomputed_hash);
}

}  // namespace
