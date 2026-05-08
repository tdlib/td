// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_macro_test_utils.h"

#include "td/utils/logging.h"
#include "td/utils/tests.h"

#include <atomic>
#include <random>

namespace {

using td::logging_macro::test::CountingLog;
using td::logging_macro::test::ScopedLoggingOverride;

std::atomic<int> VERBOSITY_NAME(macro_light_fuzz_tag){VERBOSITY_NAME(INFO)};

TEST(LoggingMacroLightFuzz, RandomizedGateMatrixMatchesReferenceBehavior) {
  CountingLog sink;
  ScopedLoggingOverride guard(&sink, VERBOSITY_NAME(DEBUG));

  std::mt19937 rng(0xA11C37u);
  // FATAL is intentionally excluded: emitting at level 0 terminates the process via process_fatal_error().
  // This fuzz target validates non-terminal runtime-gate behavior only.
  const td::vector<int> levels = {VERBOSITY_NAME(ERROR), VERBOSITY_NAME(WARNING), VERBOSITY_NAME(INFO),
                                  VERBOSITY_NAME(DEBUG), VERBOSITY_NAME(NEVER)};
  std::uniform_int_distribution<size_t> pick(0, levels.size() - 1);

  constexpr int kIterations = 10000;
  for (int i = 0; i < kIterations; i++) {
    const int runtime_level = levels[pick(rng)];
    const int tag_level = levels[pick(rng)];

    SET_VERBOSITY_LEVEL(runtime_level);
    VERBOSITY_NAME(macro_light_fuzz_tag).store(tag_level, std::memory_order_release);

    const int before = sink.writes.load(std::memory_order_relaxed);
    VLOG(macro_light_fuzz_tag) << "logging-macro-light-fuzz" << i;
    const int after = sink.writes.load(std::memory_order_relaxed);

    const int expected_delta = tag_level <= runtime_level ? 1 : 0;
    ASSERT_EQ(before + expected_delta, after);
  }
}

}  // namespace
