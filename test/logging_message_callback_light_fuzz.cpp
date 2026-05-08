// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/utils/logging.h"
#include "td/utils/tests.h"

#include <atomic>
#include <random>

namespace {

class DiscardLog final : public td::LogInterface {
 public:
  void do_append(int log_level, td::CSlice slice) final {
    (void)log_level;
    (void)slice;
  }
};

std::atomic<int> g_light_fuzz_calls{0};

void light_fuzz_callback(int verbosity_level, td::CSlice message) {
  (void)verbosity_level;
  (void)message;
  g_light_fuzz_calls.fetch_add(1, std::memory_order_relaxed);
}

TEST(LoggingMessageCallbackLightFuzz, RandomizedThresholdsPreserveSingleMessageEligibilityInvariant) {
  DiscardLog sink;
  td::LogOptions options(VERBOSITY_NAME(DEBUG), false, false);

  std::mt19937 rng(0xC0FFEE11u);
  std::uniform_int_distribution<int> level_dist(VERBOSITY_NAME(ERROR), VERBOSITY_NAME(DEBUG) + 4);
  std::bernoulli_distribution callback_enabled(0.5);

  constexpr int kIterations = 10000;
  for (int i = 0; i < kIterations; i++) {
    const int max_level = level_dist(rng);
    const int emitted_level = level_dist(rng);
    const bool enabled = callback_enabled(rng);

    g_light_fuzz_calls.store(0, std::memory_order_relaxed);
    td::set_log_message_callback(max_level, enabled ? &light_fuzz_callback : nullptr);
    {
      td::Logger logger(sink, options, emitted_level);
      logger << "light-fuzz-callback";
    }

    const int expected_calls = enabled && emitted_level <= max_level ? 1 : 0;
    ASSERT_EQ(expected_calls, g_light_fuzz_calls.load(std::memory_order_relaxed));
  }

  td::set_log_message_callback(VERBOSITY_NAME(FATAL), nullptr);
}

}  // namespace