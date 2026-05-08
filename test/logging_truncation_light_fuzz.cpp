// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/utils/logging.h"
#include "td/utils/tests.h"

#include <random>

namespace {

class CapturingLog final : public td::LogInterface {
 public:
  void do_append(int log_level, td::CSlice slice) final {
    (void)log_level;
    last_message = slice.str();
  }

  td::string last_message;
};

TEST(LoggingTruncationLightFuzz, BoundaryLengthFuzzKeepsExplicitOverflowMarker) {
  CapturingLog capture;
  td::LogOptions options(VERBOSITY_NAME(DEBUG), true, false);

  std::mt19937 rng(0xCC5F2E0Du);
  std::uniform_int_distribution<int> delta(-1024, 4096);

  constexpr int kIterations = 10000;
  for (int i = 0; i < kIterations; i++) {
    const int candidate = 128 * 1024 + delta(rng);
    const size_t size = static_cast<size_t>(td::max(candidate, 0));
    td::string payload(size, static_cast<char>('A' + (i % 20)));

    {
      td::Logger logger(capture, options, VERBOSITY_NAME(ERROR));
      logger << payload;
    }

    ASSERT_TRUE(!capture.last_message.empty());
    // Actual truncation threshold with fix_newlines=true (no header):
    //   BUFFER_SIZE - RESERVED_SIZE = 131072 - 30 = 131042.
    // Using 131072 (the old constant) silently missed the [131042, 131071] band.
    constexpr size_t kTruncThresh = 128u * 1024u - 30u;  // 131042
    if (size >= kTruncThresh) {
      ASSERT_TRUE(capture.last_message.find("[truncated]") != td::string::npos);
    }
  }
}

}  // namespace
