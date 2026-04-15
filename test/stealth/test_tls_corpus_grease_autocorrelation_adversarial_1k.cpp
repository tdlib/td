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

#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

using namespace td;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;

constexpr uint64 kCorpusIterations = kFullIterations;
constexpr int32 kUnixTime = 1712345678;

ParsedClientHello build_chrome133_hello(uint64 seed) {
  MockRng rng(corpus_seed_for_iteration(seed, kCorpusIterations));
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                 BrowserProfile::Chrome133, EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  return parsed.move_as_ok();
}

struct GreaseSlots final {
  uint16 cipher_grease{0};
  uint16 ext_first_grease{0};
  uint16 ext_last_grease{0};
  uint16 supported_groups_grease{0};
  uint16 key_share_grease{0};
  uint16 supported_versions_grease{0};
};

GreaseSlots collect_grease_slots(const ParsedClientHello &hello) {
  GreaseSlots slots;
  auto cipher_suites = parse_cipher_suite_vector(hello.cipher_suites).move_as_ok();
  for (auto cs : cipher_suites) {
    if (is_grease_value(cs)) {
      slots.cipher_grease = cs;
      break;
    }
  }
  for (const auto &ext : hello.extensions) {
    if (is_grease_value(ext.type)) {
      slots.ext_first_grease = ext.type;
      break;
    }
  }
  for (auto it = hello.extensions.rbegin(); it != hello.extensions.rend(); ++it) {
    if (is_grease_value(it->type)) {
      slots.ext_last_grease = it->type;
      break;
    }
  }
  if (!hello.supported_groups.empty() && is_grease_value(hello.supported_groups.front())) {
    slots.supported_groups_grease = hello.supported_groups.front();
  }
  if (!hello.key_share_entries.empty() && is_grease_value(hello.key_share_entries.front().group)) {
    slots.key_share_grease = hello.key_share_entries.front().group;
  }
  auto *sv = find_extension(hello, 0x002Bu);
  if (sv != nullptr && sv->value.size() >= 3) {
    slots.supported_versions_grease =
        static_cast<uint16>((static_cast<uint8>(sv->value[1]) << 8) | static_cast<uint8>(sv->value[2]));
  }
  return slots;
}

// -- Intra-hello uniqueness: all GREASE slots within ONE hello must be distinct --

TEST(GreaseIntraHelloAdversarial1k, AllGreaseSlotsWithinSingleChromeHelloAreNotAllIdentical) {
  if (!is_nightly_corpus_enabled()) {
    return;
  }
  // Chrome produces 6 independent GREASE draws from a pool of 16 values.
  // By birthday paradox, pairwise collisions are common (~60% of hellos have at least one).
  // But ALL 6 being identical would indicate lazy PRNG reuse (prob ≈ (1/16)^5 ≈ 10^-6).
  // Across 1024 runs, the number with all 6 identical must be near-zero.
  uint32 all_identical_count = 0;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = build_chrome133_hello(seed);
    auto slots = collect_grease_slots(hello);
    std::unordered_set<uint16> seen;
    seen.insert(slots.cipher_grease);
    seen.insert(slots.ext_first_grease);
    seen.insert(slots.ext_last_grease);
    seen.insert(slots.supported_groups_grease);
    seen.insert(slots.key_share_grease);
    seen.insert(slots.supported_versions_grease);
    if (seen.size() == 1u) {
      all_identical_count++;
    }
  }
  // Allow at most 5 hellos with all-identical GREASE (extremely conservative)
  ASSERT_TRUE(all_identical_count <= 5u);
}

// -- Autocorrelation: detect periodic GREASE sequences --

double compute_autocorrelation(const vector<double> &series, size_t lag) {
  CHECK(lag < series.size());
  size_t n = series.size();
  double mean = 0;
  for (auto v : series) {
    mean += v;
  }
  mean /= static_cast<double>(n);
  double numerator = 0;
  double denominator = 0;
  for (size_t i = 0; i < n; i++) {
    denominator += (series[i] - mean) * (series[i] - mean);
  }
  if (denominator < 1e-12) {
    return 0.0;
  }
  for (size_t i = 0; i + lag < n; i++) {
    numerator += (series[i] - mean) * (series[i + lag] - mean);
  }
  return numerator / denominator;
}

TEST(GreaseIntraHelloAdversarial1k, CipherGreaseSequenceHasNoPeriodicPattern) {
  if (!is_nightly_corpus_enabled()) {
    return;
  }
  vector<double> cipher_grease_values;
  cipher_grease_values.reserve(kCorpusIterations);
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto slots = collect_grease_slots(build_chrome133_hello(seed));
    cipher_grease_values.push_back(static_cast<double>(slots.cipher_grease));
  }
  // Check autocorrelation at several lags. For a good PRNG, autocorrelation should be near 0.
  for (size_t lag : {1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u, 256u, 512u}) {
    auto acf = compute_autocorrelation(cipher_grease_values, lag);
    ASSERT_TRUE(std::abs(acf) < 0.15);
  }
}

TEST(GreaseIntraHelloAdversarial1k, ExtensionPositionSequenceHasNoPeriodicPattern) {
  if (!is_nightly_corpus_enabled()) {
    return;
  }
  // Track position of renegotiation_info (0xFF01) in each hello
  vector<double> positions;
  positions.reserve(kCorpusIterations);
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto hello = build_chrome133_hello(seed);
    auto seq = non_grease_extension_sequence(hello);
    int pos = -1;
    for (size_t i = 0; i < seq.size(); i++) {
      if (seq[i] == 0xFF01u) {
        pos = static_cast<int>(i);
        break;
      }
    }
    positions.push_back(static_cast<double>(pos));
  }
  for (size_t lag : {1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u, 256u, 512u}) {
    auto acf = compute_autocorrelation(positions, lag);
    ASSERT_TRUE(std::abs(acf) < 0.15);
  }
}

// -- GREASE values not linearly correlated with seed index --

TEST(GreaseIntraHelloAdversarial1k, GreaseCipherNotLinearlyCorrelatedWithSeed) {
  if (!is_nightly_corpus_enabled()) {
    return;
  }
  // Compute Pearson correlation between seed index and cipher GREASE value
  double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0, sum_y2 = 0;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    double x = static_cast<double>(seed);
    double y = static_cast<double>(collect_grease_slots(build_chrome133_hello(seed)).cipher_grease);
    sum_x += x;
    sum_y += y;
    sum_xy += x * y;
    sum_x2 += x * x;
    sum_y2 += y * y;
  }
  double n = static_cast<double>(kCorpusIterations);
  double denom = std::sqrt((n * sum_x2 - sum_x * sum_x) * (n * sum_y2 - sum_y * sum_y));
  double r = denom < 1e-12 ? 0.0 : (n * sum_xy - sum_x * sum_y) / denom;
  // |r| should be near 0 for no linear correlation
  ASSERT_TRUE(std::abs(r) < 0.1);
}

// -- Chi-squared contingency table: cipher GREASE vs extension GREASE --

TEST(GreaseIntraHelloAdversarial1k, CipherVsExtensionGreaseContingencyTableNotDiagonal) {
  if (!is_nightly_corpus_enabled()) {
    return;
  }
  // Build a contingency table of (cipher_grease, ext_first_grease) pairs
  std::unordered_map<uint32, uint32> pair_counts;
  std::unordered_map<uint16, uint32> cipher_marginal;
  std::unordered_map<uint16, uint32> ext_marginal;

  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    auto slots = collect_grease_slots(build_chrome133_hello(seed));
    auto pair_key = (static_cast<uint32>(slots.cipher_grease) << 16) | slots.ext_first_grease;
    pair_counts[pair_key]++;
    cipher_marginal[slots.cipher_grease]++;
    ext_marginal[slots.ext_first_grease]++;
  }

  // Check that diagonal entries don't dominate: less than 30% of total on the diagonal
  uint32 diagonal_count = 0;
  for (const auto &it : pair_counts) {
    auto cipher_val = static_cast<uint16>(it.first >> 16);
    auto ext_val = static_cast<uint16>(it.first & 0xFFFF);
    if (cipher_val == ext_val) {
      diagonal_count += it.second;
    }
  }
  // If independent: diagonal should hold ~1/16 = 6.25% of pairs.
  // If correlated (lazy PRNG): diagonal would hold close to 100%.
  ASSERT_TRUE(diagonal_count < kCorpusIterations * 25 / 100);
}

// -- ECH payload length autocorrelation --

TEST(GreaseIntraHelloAdversarial1k, EchPayloadLengthSequenceHasNoPeriodicPattern) {
  if (!is_nightly_corpus_enabled()) {
    return;
  }
  vector<double> lengths;
  lengths.reserve(kCorpusIterations);
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    MockRng rng(corpus_seed_for_iteration(seed, kCorpusIterations));
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
    auto parsed = parse_tls_client_hello(wire);
    CHECK(parsed.is_ok());
    lengths.push_back(static_cast<double>(parsed.ok().ech_payload_length));
  }
  for (size_t lag : {1u, 2u, 4u, 8u, 16u, 32u, 64u}) {
    auto acf = compute_autocorrelation(lengths, lag);
    ASSERT_TRUE(std::abs(acf) < 0.15);
  }
}

// -- Wire size autocorrelation for ECH-disabled Chrome (padding entropy) --

TEST(GreaseIntraHelloAdversarial1k, WireSizeSequenceHasNoPeriodicPattern) {
  if (!is_nightly_corpus_enabled()) {
    return;
  }
  vector<double> sizes;
  sizes.reserve(kCorpusIterations);
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    MockRng rng(corpus_seed_for_iteration(seed, kCorpusIterations));
    sizes.push_back(
        static_cast<double>(build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                               BrowserProfile::Chrome133, EchMode::Disabled, rng)
                                .size()));
  }
  for (size_t lag : {1u, 2u, 4u, 8u, 16u, 32u, 64u}) {
    auto acf = compute_autocorrelation(sizes, lag);
    ASSERT_TRUE(std::abs(acf) < 0.15);
  }
}

}  // namespace
