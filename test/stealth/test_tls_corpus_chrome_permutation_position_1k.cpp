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

#include <set>

namespace {

using namespace td;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;

const uint64 kCorpusIterations = spot_or_full_corpus_iterations();
constexpr int32 kUnixTime = 1712345678;
const uint64 kDistinctSequenceFloor = is_nightly_corpus_enabled() ? 1000u : 60u;

ParsedClientHello build_chrome_hello(BrowserProfile profile, EchMode ech_mode, uint64 seed) {
  MockRng rng(corpus_seed_for_iteration(seed, kCorpusIterations));
  auto wire =
      build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile, ech_mode, rng);
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  return parsed.move_as_ok();
}

int find_non_grease_position(const ParsedClientHello &hello, uint16 target_type) {
  int position = 0;
  for (auto ext_type : non_grease_extension_sequence(hello)) {
    if (ext_type == target_type) {
      return position;
    }
    position++;
  }
  return -1;
}

string sequence_key(const vector<uint16> &sequence) {
  string result;
  for (auto ext_type : sequence) {
    if (!result.empty()) {
      result += ",";
    }
    result += hex_u16(ext_type);
  }
  return result;
}

TEST(ChromePermutationPosition1k, Chrome133ProducesManyDistinctNonGreaseSequences) {
  std::set<string> sequences;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    sequences.insert(sequence_key(
        non_grease_extension_sequence(build_chrome_hello(BrowserProfile::Chrome133, EchMode::Disabled, seed))));
  }
  ASSERT_TRUE(sequences.size() >= kDistinctSequenceFloor);
}

TEST(ChromePermutationPosition1k, Chrome131ProducesManyDistinctNonGreaseSequences) {
  std::set<string> sequences;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    sequences.insert(sequence_key(
        non_grease_extension_sequence(build_chrome_hello(BrowserProfile::Chrome131, EchMode::Disabled, seed))));
  }
  ASSERT_TRUE(sequences.size() >= kDistinctSequenceFloor);
}

TEST(ChromePermutationPosition1k, ExtensionAtAbsolutePositionZeroIsAlwaysGrease) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = build_chrome_hello(BrowserProfile::Chrome133, EchMode::Disabled, seed);
    ASSERT_FALSE(hello.extensions.empty());
    ASSERT_TRUE(is_grease_value(hello.extensions.front().type));
  }
}

TEST(ChromePermutationPosition1k, LastNonPaddingExtensionIsAlwaysGrease) {
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = build_chrome_hello(BrowserProfile::Chrome133, EchMode::Disabled, seed);
    ASSERT_FALSE(hello.extensions.empty());
    auto it = hello.extensions.rbegin();
    while (it != hello.extensions.rend() && it->type == 0x0015u) {
      ++it;
    }
    ASSERT_TRUE(it != hello.extensions.rend());
    ASSERT_TRUE(is_grease_value(it->type));
  }
}

TEST(ChromePermutationPosition1k, RenegotiationInfoAppearsInManyNonGreasePositions) {
  std::set<int> positions;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto position =
        find_non_grease_position(build_chrome_hello(BrowserProfile::Chrome133, EchMode::Disabled, seed), 0xFF01u);
    ASSERT_TRUE(position >= 0);
    positions.insert(position);
  }
  ASSERT_TRUE(positions.size() >= 8u);
}

TEST(ChromePermutationPosition1k, SignatureAlgorithmsAppearsInManyNonGreasePositions) {
  std::set<int> positions;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto position =
        find_non_grease_position(build_chrome_hello(BrowserProfile::Chrome133, EchMode::Disabled, seed), 0x000Du);
    ASSERT_TRUE(position >= 0);
    positions.insert(position);
  }
  ASSERT_TRUE(positions.size() >= 8u);
}

TEST(ChromePermutationPosition1k, SessionTicketAppearsInManyNonGreasePositions) {
  std::set<int> positions;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto position =
        find_non_grease_position(build_chrome_hello(BrowserProfile::Chrome133, EchMode::Disabled, seed), 0x0023u);
    ASSERT_TRUE(position >= 0);
    positions.insert(position);
  }
  ASSERT_TRUE(positions.size() >= 8u);
}

TEST(ChromePermutationPosition1k, AlpsAppearsInManyNonGreasePositions) {
  std::set<int> positions;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto position =
        find_non_grease_position(build_chrome_hello(BrowserProfile::Chrome133, EchMode::Disabled, seed), 0x44CDu);
    ASSERT_TRUE(position >= 0);
    positions.insert(position);
  }
  ASSERT_TRUE(positions.size() >= 8u);
}

TEST(ChromePermutationPosition1k, StatusRequestAppearsInManyNonGreasePositions) {
  std::set<int> positions;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto position =
        find_non_grease_position(build_chrome_hello(BrowserProfile::Chrome133, EchMode::Disabled, seed), 0x0005u);
    ASSERT_TRUE(position >= 0);
    positions.insert(position);
  }
  ASSERT_TRUE(positions.size() >= 8u);
}

TEST(ChromePermutationPosition1k, EchExtensionAppearsInManyNonGreasePositions) {
  std::set<int> positions;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto position =
        find_non_grease_position(build_chrome_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, seed), 0xFE0Du);
    ASSERT_TRUE(position >= 0);
    positions.insert(position);
  }
  ASSERT_TRUE(positions.size() >= 6u);
}

TEST(ChromePermutationPosition1k, SniAppearsInManyNonGreasePositions) {
  std::set<int> positions;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto position =
        find_non_grease_position(build_chrome_hello(BrowserProfile::Chrome133, EchMode::Disabled, seed), 0x0000u);
    ASSERT_TRUE(position >= 0);
    positions.insert(position);
  }
  ASSERT_TRUE(positions.size() >= 8u);
}

TEST(ChromePermutationPosition1k, RenegotiationInfoIsNotPinnedToLastNonGreasePosition) {
  size_t last_position_hits = 0;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = build_chrome_hello(BrowserProfile::Chrome133, EchMode::Disabled, seed);
    auto sequence = non_grease_extension_sequence(hello);
    ASSERT_FALSE(sequence.empty());
    if (find_non_grease_position(hello, 0xFF01u) == static_cast<int>(sequence.size() - 1)) {
      last_position_hits++;
    }
  }
  ASSERT_TRUE(last_position_hits < kCorpusIterations * 95u / 100u);
}

TEST(ChromePermutationPosition1k, FirefoxSequenceRemainsFixedAcrossAllSeeds) {
  std::set<string> sequences;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    sequences.insert(sequence_key(
        non_grease_extension_sequence(build_chrome_hello(BrowserProfile::Firefox148, EchMode::Disabled, seed))));
  }
  ASSERT_EQ(1u, sequences.size());
}

}  // namespace