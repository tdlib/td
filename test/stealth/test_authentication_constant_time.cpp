//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/tests.h"

#include <chrono>
#include <climits>
#include <cstring>
#include <vector>

namespace {

using namespace td;

// Constant-time comparison validation for authentication primitives.
// The tests use coarse scheduler-tolerant bounds and run serially.

class AuthenticationTimingConsistency {
 public:
  static constexpr int SAMPLES = 100;
  static constexpr int HASH_SIZE = 32;

  struct TimingMeasurement {
    std::vector<long long> success_times_us;
    std::vector<long long> failure_times_us;

    long long mean_success() const {
      if (success_times_us.empty())
        return 0;
      long long sum = 0;
      for (auto t : success_times_us)
        sum += t;
      return sum / static_cast<long long>(success_times_us.size());
    }

    long long mean_failure() const {
      if (failure_times_us.empty())
        return 0;
      long long sum = 0;
      for (auto t : failure_times_us)
        sum += t;
      return sum / static_cast<long long>(failure_times_us.size());
    }

    long long variance_failure() const {
      if (failure_times_us.empty())
        return 0;
      long long mean = mean_failure();
      long long sum = 0;
      for (auto t : failure_times_us) {
        long long diff = t - mean;
        sum += diff * diff;
      }
      return sum / static_cast<long long>(failure_times_us.size());
    }
  };

  static TimingMeasurement measure_hmac_comparison_timing() {
    TimingMeasurement result;

    string correct_hash(HASH_SIZE, 'A');
    string test_hash(HASH_SIZE, 'A');

    // Measure successful comparison (all bytes match)
    for (int i = 0; i < SAMPLES; i++) {
      auto start = std::chrono::high_resolution_clock::now();

      bool match = constant_time_equals(correct_hash, test_hash);
      ASSERT_TRUE(match);

      auto end = std::chrono::high_resolution_clock::now();
      auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
      result.success_times_us.push_back(duration_us);
    }

    // Measure failed comparisons with mismatch at different positions
    for (int mismatch_pos = 0; mismatch_pos < HASH_SIZE; mismatch_pos++) {
      test_hash[mismatch_pos] = 'B';  // Flip a byte

      auto start = std::chrono::high_resolution_clock::now();

      bool match = constant_time_equals(correct_hash, test_hash);
      ASSERT_FALSE(match);

      auto end = std::chrono::high_resolution_clock::now();
      auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
      result.failure_times_us.push_back(duration_us);

      test_hash[mismatch_pos] = 'A';  // Reset
    }

    return result;
  }

  static void validate_constant_time_comparison() {
    auto timing = measure_hmac_comparison_timing();

    [[maybe_unused]] long long success_mean = timing.mean_success();
    long long failure_mean = timing.mean_failure();
    [[maybe_unused]] long long failure_var = timing.variance_failure();

    // Critical: failure time should NOT depend on position of mismatch
    // All failure measurements should be within 20% of each other
    long long max_failure = 0, min_failure = LLONG_MAX;
    for (auto t : timing.failure_times_us) {
      max_failure = std::max(max_failure, t);
      min_failure = std::min(min_failure, t);
    }

    long long failure_range = max_failure - min_failure;
    long long failure_median = failure_mean;

    // If range is > 20% of median, timing is data-dependent (VULNERABILITY)
    if (failure_median > 0) {
      long long tolerance = failure_median / 5;  // 20% tolerance
      ASSERT_TRUE(failure_range < tolerance);
      // TIMING SIDECHANNEL DETECTION: if this fails, check min/max spread
      // Min time: min_failure, Max time: max_failure, Spread: failure_range (check < tolerance)
    }
  }

  static void validate_string_comparison_no_early_exit() {
    // Verify that string comparison doesn't short-circuit on first mismatch
    // by testing that all byte positions take roughly the same time

    std::vector<long long> position_times;
    constexpr int iterations_per_position = 20000;

    string template_str(HASH_SIZE, 'A');

    for (int pos = 0; pos < HASH_SIZE; pos++) {
      string test_str = template_str;
      test_str[pos] = 'X';  // Mismatch at position 'pos'

      auto start = std::chrono::high_resolution_clock::now();
      bool accumulated = false;
      for (int i = 0; i < iterations_per_position; i++) {
        accumulated ^= constant_time_equals(template_str, test_str);
      }
      auto end = std::chrono::high_resolution_clock::now();
      ASSERT_FALSE(accumulated);
      auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
      position_times.push_back(duration_us);
    }

    // Use a median-relative bound to avoid false positives from occasional
    // scheduler spikes while still catching obvious early-exit behavior.
    auto sorted = position_times;
    std::sort(sorted.begin(), sorted.end());
    const auto median = sorted[sorted.size() / 2];
    if (median > 0) {
      for (auto t : sorted) {
        ASSERT_TRUE(t < median * 6);
      }
    }
  }

  static void validate_hmac_verification_constant_time() {
    // HMAC verification is the critical path; it must not leak key information
    // via timing sidechannels

    string key(32, 'K');
    string message = "test message for hmac verification";
    string correct_hmac(32, '\0');

    // Compute real HMAC
    hmac_sha256(key, message, correct_hmac);

    std::vector<long long> verification_times;

    // Try HMAC with single-byte differences at different positions
    for (int pos = 0; pos < 16; pos++) {  // Test first half of HMAC
      string test_hmac = correct_hmac;
      test_hmac[pos] ^= 0x01;  // Flip one bit

      auto start = std::chrono::high_resolution_clock::now();

      [[maybe_unused]] bool match = constant_time_equals(correct_hmac, test_hmac);

      auto end = std::chrono::high_resolution_clock::now();
      auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
      verification_times.push_back(duration_us);
    }

    // All verification failures should take same time
    long long max_time = *std::max_element(verification_times.begin(), verification_times.end());
    long long min_time = *std::min_element(verification_times.begin(), verification_times.end());
    long long spread = max_time - min_time;

    if (max_time > 0) {
      long long tolerance = max_time / 4;  // 25% tolerance
      ASSERT_TRUE(spread < tolerance);
      // HMAC timing sidechannel: verify spread is low
    }
  }
};

TEST(AuthenticationTimingConsistency, ComparisonTimeIndependentOfMismatchPosition) {
  AuthenticationTimingConsistency::validate_constant_time_comparison();
}

TEST(AuthenticationTimingConsistency, StringComparisonNoEarlyExit) {
  AuthenticationTimingConsistency::validate_string_comparison_no_early_exit();
}

TEST(AuthenticationTimingConsistency, HmacVerificationConstantTime) {
  AuthenticationTimingConsistency::validate_hmac_verification_constant_time();
}

}  // namespace
