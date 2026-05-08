// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/utils/logging.h"
#include "td/utils/tests.h"

#include <limits>
#include <random>

namespace {

constexpr int VERBOSITY_NAME(macro_adversarial_quiet) = VERBOSITY_NAME(ERROR);
constexpr int VERBOSITY_NAME(macro_adversarial_loud) = VERBOSITY_NAME(NEVER) + 1;

static_assert(!LOG_IS_STRIPPED(macro_adversarial_quiet), "ERROR-level custom tag must not be compile-time stripped");
static_assert(LOG_IS_STRIPPED(macro_adversarial_loud), "Custom tags above STRIP_LOG must be compile-time stripped");

TEST(LoggingMacroAdversarial, StripPredicateHoldsAtExtremeIntegerBoundaries) {
  const td::vector<int> strip_levels = {
      std::numeric_limits<int>::min(), VERBOSITY_NAME(PLAIN), VERBOSITY_NAME(FATAL),           VERBOSITY_NAME(WARNING),
      VERBOSITY_NAME(DEBUG),           VERBOSITY_NAME(NEVER), std::numeric_limits<int>::max(),
  };
  const td::vector<int> build_levels = {
      std::numeric_limits<int>::min(), VERBOSITY_NAME(FATAL),           VERBOSITY_NAME(DEBUG),
      VERBOSITY_NAME(NEVER),           std::numeric_limits<int>::max(),
  };

  for (auto strip_level : strip_levels) {
    for (auto build_level : build_levels) {
      ASSERT_EQ(strip_level > build_level, td::detail::is_log_stripped(strip_level, build_level));
    }
  }
}

TEST(LoggingMacroAdversarial, LightFuzzPredicateNeverDeviatesFromReferenceRelation) {
  std::mt19937 rng(0xC0A17F3u);
  std::uniform_int_distribution<int> dist(-4096, 4096);

  constexpr int kIterations = 10000;
  for (int i = 0; i < kIterations; i++) {
    const int strip_level = dist(rng);
    const int build_level = dist(rng);
    ASSERT_EQ(strip_level > build_level, td::detail::is_log_stripped(strip_level, build_level));
  }
}

}  // namespace
