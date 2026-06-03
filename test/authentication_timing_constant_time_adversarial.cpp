// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    auto b = static_cast<unsigned char>(c);
    if (b == ' ' || b == '\t' || b == '\r' || b == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

td::string extract_region(std::string_view source, td::Slice begin_marker, td::Slice end_marker) {
  auto begin = source.find(begin_marker.str());
  CHECK(begin != td::string::npos);
  auto end = source.find(end_marker.str(), begin + begin_marker.size());
  CHECK(end != td::string::npos);
  CHECK(end > begin);
  return td::string(source.substr(begin, end - begin));
}

}  // namespace

TEST(AuthenticationTimingConstantTimeAdversarial, HmacTimingValidationRejectsZeroToleranceDivisionPattern) {
  auto source = td::mtproto::test::read_repo_text_file("test/stealth/test_authentication_constant_time.cpp");
  auto region =
      normalize_for_contract(extract_region(source, "static void validate_hmac_verification_constant_time() {", "};"));

  ASSERT_EQ(td::string::npos, region.find("longlongtolerance=max_time/4;"));
  ASSERT_EQ(td::string::npos, region.find("ASSERT_TRUE(spread<tolerance);"));
}

TEST(AuthenticationTimingConstantTimeAdversarial, HmacTimingValidationAvoidsSingleShotComparisonMeasurement) {
  auto source = td::mtproto::test::read_repo_text_file("test/stealth/test_authentication_constant_time.cpp");
  auto region =
      normalize_for_contract(extract_region(source, "static void validate_hmac_verification_constant_time() {", "};"));

  ASSERT_EQ(td::string::npos, region.find("[[maybe_unused]]boolmatch=constant_time_equals(correct_hmac,test_hmac);"));
}

TEST(AuthenticationTimingConstantTimeAdversarial, HmacTimingValidationDoesNotSkipZeroResolutionMeasurements) {
  auto source = td::mtproto::test::read_repo_text_file("test/stealth/test_authentication_constant_time.cpp");
  auto region =
      normalize_for_contract(extract_region(source, "static void validate_hmac_verification_constant_time() {", "};"));

  ASSERT_EQ(td::string::npos, region.find("if(median>0){"));
}

// ---------------------------------------------------------------------------
// Genuine adversarial timing measurement tests.
// These call constant_time_equals with crafted inputs and verify that the
// elapsed wall-clock time does not depend on which byte position differs.
// ---------------------------------------------------------------------------

TEST(AuthenticationTimingConstantTimeAdversarial, AdversarialTimingConstantTimeActualMeasurement) {
  // Measure elapsed time for full-match, early-mismatch, and late-mismatch
  // over many iterations.  A correct constant-time implementation should show
  // negligible difference between the three scenarios.

  constexpr int HASH_SIZE = 32;
  constexpr int ITERATIONS = 2000;

  td::string reference(HASH_SIZE, '\x55');

  // --- full match ---
  td::string full_match = reference;

  // --- early mismatch (byte 0) ---
  td::string early_mismatch = reference;
  early_mismatch[0] ^= 0xFF;

  // --- late mismatch (last byte) ---
  td::string late_mismatch = reference;
  late_mismatch[HASH_SIZE - 1] ^= 0xFF;

  auto measure_median_ns = [&](const td::string &a, const td::string &b) -> long long {
    std::vector<long long> samples;
    samples.reserve(ITERATIONS);
    for (int i = 0; i < ITERATIONS; i++) {
      auto start = std::chrono::high_resolution_clock::now();
      volatile bool result = td::constant_time_equals(a, b);
      (void)result;
      auto end = std::chrono::high_resolution_clock::now();
      samples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
  };

  long long median_match = measure_median_ns(reference, full_match);
  long long median_early = measure_median_ns(reference, early_mismatch);
  long long median_late = measure_median_ns(reference, late_mismatch);

  // All three medians must be positive (sanity check for clock resolution).
  // If they are zero the test is inconclusive rather than failing, so we
  // skip the ratio check in that degenerate case.
  if (median_match > 0 && median_early > 0 && median_late > 0) {
    // Early and late mismatch should be within 3x of each other.
    // A naive memcmp would show early_mismatch much faster than late.
    ASSERT_TRUE(median_early < median_late * 3);
    ASSERT_TRUE(median_late < median_early * 3);

    // Match vs mismatch should also be comparable.
    ASSERT_TRUE(median_match < median_early * 3);
    ASSERT_TRUE(median_early < median_match * 3);
  }
}

TEST(AuthenticationTimingConstantTimeAdversarial, AdversarialTimingEarlyVsLateMismatch) {
  // More rigorous per-position test: measure the time to compare strings that
  // differ only at one byte position, then verify all positions take
  // approximately the same time (within 2x of median).  This directly
  // detects an early-exit / short-circuit implementation.

  constexpr int HASH_SIZE = 32;
  constexpr int ITERATIONS_PER_POS = 5000;

  td::string reference(HASH_SIZE, '\xAA');

  std::vector<long long> per_position_medians;
  per_position_medians.reserve(HASH_SIZE);

  for (int pos = 0; pos < HASH_SIZE; pos++) {
    td::string test_str = reference;
    test_str[pos] ^= 0xFF;

    std::vector<long long> samples;
    samples.reserve(ITERATIONS_PER_POS);

    for (int i = 0; i < ITERATIONS_PER_POS; i++) {
      auto start = std::chrono::high_resolution_clock::now();
      volatile bool result = td::constant_time_equals(reference, test_str);
      (void)result;
      auto end = std::chrono::high_resolution_clock::now();
      samples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    }

    std::sort(samples.begin(), samples.end());
    per_position_medians.push_back(samples[samples.size() / 2]);
  }

  // Compute the overall median of per-position medians.
  auto sorted_medians = per_position_medians;
  std::sort(sorted_medians.begin(), sorted_medians.end());
  long long overall_median = sorted_medians[sorted_medians.size() / 2];

  // Every per-position median must be within 2x of the overall median.
  // This would fail dramatically for a naive memcmp where position 0
  // mismatch is ~32x faster than position 31 mismatch.
  if (overall_median > 0) {
    for (int pos = 0; pos < HASH_SIZE; pos++) {
      long long t = per_position_medians[static_cast<size_t>(pos)];
      ASSERT_TRUE(t * 2 >= overall_median);
      ASSERT_TRUE(t <= overall_median * 2);
    }
  }
}

TEST(AuthenticationTimingConstantTimeAdversarial, AdversarialTimingHmacDigestVerification) {
  // End-to-end test: compute a real HMAC-SHA256, then verify that comparing
  // the correct digest against digests that differ at the first vs last byte
  // takes the same amount of time.

  td::string key(32, 'K');
  td::string message = "adversarial timing test for hmac digest verification path";
  td::string correct_hmac(32, '\0');
  td::hmac_sha256(key, message, correct_hmac);

  constexpr int ITERATIONS = 3000;

  // Build two adversarial digests: first-byte-wrong and last-byte-wrong.
  td::string hmac_first_wrong = correct_hmac;
  hmac_first_wrong[0] ^= 0x01;

  td::string hmac_last_wrong = correct_hmac;
  hmac_last_wrong[31] ^= 0x01;

  auto measure_batch_ns = [&](const td::string &a, const td::string &b) -> long long {
    // Measure a batch of iterations to amplify any timing difference and
    // avoid clock-resolution floor effects.
    std::vector<long long> batch_times;
    constexpr int BATCHES = 50;
    constexpr int BATCH_SIZE = ITERATIONS / BATCHES;
    batch_times.reserve(BATCHES);

    for (int batch = 0; batch < BATCHES; batch++) {
      bool accumulated = false;
      auto start = std::chrono::high_resolution_clock::now();
      for (int i = 0; i < BATCH_SIZE; i++) {
        accumulated ^= td::constant_time_equals(a, b);
      }
      auto end = std::chrono::high_resolution_clock::now();
      // Use accumulated to prevent dead-code elimination.
      ASSERT_FALSE(accumulated);
      batch_times.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    }

    std::sort(batch_times.begin(), batch_times.end());
    return batch_times[batch_times.size() / 2];
  };

  long long median_first_wrong = measure_batch_ns(correct_hmac, hmac_first_wrong);
  long long median_last_wrong = measure_batch_ns(correct_hmac, hmac_last_wrong);
  long long median_correct = measure_batch_ns(correct_hmac, correct_hmac);

  // All medians must be positive for meaningful comparison.
  ASSERT_TRUE(median_first_wrong > 0);
  ASSERT_TRUE(median_last_wrong > 0);
  ASSERT_TRUE(median_correct > 0);

  // First-byte-wrong vs last-byte-wrong must be within 2x of each other.
  ASSERT_TRUE(median_first_wrong < median_last_wrong * 2);
  ASSERT_TRUE(median_last_wrong < median_first_wrong * 2);

  // Correct match vs any mismatch must also be comparable.
  ASSERT_TRUE(median_correct < median_first_wrong * 2);
  ASSERT_TRUE(median_first_wrong < median_correct * 2);
}
