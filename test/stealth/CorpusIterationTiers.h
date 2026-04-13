// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/utils/common.h"

#include <cstdlib>
#include <limits>

namespace td {
namespace mtproto {
namespace test {

inline constexpr uint64 kQuickIterations = 3;
inline constexpr uint64 kSpotIterations = 64;
inline constexpr uint64 kFullIterations = 1024;

inline bool is_nightly_corpus_enabled() noexcept {
  auto *value = std::getenv("TD_NIGHTLY_CORPUS");
  return value != nullptr && value[0] != '\0' && value[0] != '0';
}

inline uint64 spot_or_full_corpus_iterations() noexcept {
  return is_nightly_corpus_enabled() ? kFullIterations : kSpotIterations;
}

inline uint64 quick_corpus_seed(uint64 iteration_index) noexcept {
  switch (iteration_index) {
    case 0:
      return 0;
    case 1:
      return 1;
    default:
      return std::numeric_limits<uint64>::max();
  }
}

inline uint64 corpus_seed_for_iteration(uint64 iteration_index, uint64 total_iterations) noexcept {
  if (total_iterations == kQuickIterations) {
    return quick_corpus_seed(iteration_index);
  }
  return iteration_index;
}

}  // namespace test
}  // namespace mtproto
}  // namespace td