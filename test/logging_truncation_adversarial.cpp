// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/utils/logging.h"
#include "td/utils/tests.h"

#include <random>

namespace {

using td::logging_hardening::test::count_occurrences;

class CapturingLog final : public td::LogInterface {
 public:
  void do_append(int log_level, td::CSlice slice) final {
    (void)log_level;
    messages.push_back(slice.str());
  }

  td::vector<td::string> messages;
};

TEST(LoggingTruncationAdversarial, OversizedPayloadAlwaysGetsTruncationMarker) {
  CapturingLog capture;
  td::LogOptions options(VERBOSITY_NAME(DEBUG), true, false);

  const td::vector<size_t> attack_sizes = {131072, 131073, 200000, 400000};
  for (auto size : attack_sizes) {
    td::string payload(size, 'X');
    {
      td::Logger logger(capture, options, VERBOSITY_NAME(ERROR));
      logger << payload;
    }
    ASSERT_TRUE(!capture.messages.empty());
    const auto &line = capture.messages.back();
    ASSERT_TRUE(line.find("[truncated]") != td::string::npos);
    ASSERT_EQ(1u, count_occurrences(line, "[truncated]"));
  }
}

TEST(LoggingTruncationAdversarial, LightFuzzBoundaryAttackNearBufferLimit) {
  CapturingLog capture;
  td::LogOptions options(VERBOSITY_NAME(DEBUG), true, false);

  std::mt19937 rng(0xD1A62B9u);
  std::uniform_int_distribution<int> delta(-512, 4096);

  constexpr int kIterations = 10000;
  // Actual truncation threshold with fix_newlines=true (no header):
  //   BUFFER_SIZE - RESERVED_SIZE = 131072 - 30 = 131042, NOT 131072.
  // Sizes in [131042, 131071] overflow via the '\n' append in the destructor,
  // NOT via the payload write itself.  The old constant 131072 missed them.
  constexpr size_t kTruncThresh = 128u * 1024u - 30u;  // 131042

  for (int i = 0; i < kIterations; i++) {
    const int candidate = 128 * 1024 + delta(rng);
    const size_t size = static_cast<size_t>(td::max(candidate, 0));
    td::string payload(size, static_cast<char>('a' + (i % 26)));

    {
      td::Logger logger(capture, options, VERBOSITY_NAME(ERROR));
      logger << payload;
    }

    ASSERT_TRUE(!capture.messages.empty());
    const auto &line = capture.messages.back();
    if (size >= kTruncThresh) {
      ASSERT_TRUE(line.find("[truncated]") != td::string::npos);
    }
  }
}

// With fix_newlines=false the payload itself must overflow available_size=131071,
// so the threshold is BUFFER_SIZE=131072, not 131042.  These two paths must be
// independently verified.
TEST(LoggingTruncationAdversarial, FixNewlinesFalseThresholdIsBufferSizeNotBufferSizeMinusReserved) {
  CapturingLog capture;
  // fix_newlines=false: destructor does NOT call sb_ << '\n',
  // so overflow only happens from the payload itself.
  td::LogOptions options_no_nl(VERBOSITY_NAME(DEBUG), /*fix_newlines=*/false, /*add_info=*/false);

  // Payload = 131071: fits in available_size (131071), no truncation.
  {
    const td::string payload(131071u, 'X');
    capture.messages.clear();
    {
      td::Logger logger(capture, options_no_nl, VERBOSITY_NAME(ERROR));
      logger << payload;
    }
    ASSERT_TRUE(!capture.messages.empty());
    ASSERT_TRUE(capture.messages.back().find("[truncated]") == td::string::npos);
  }

  // Payload = 131072: overflows available_size; truncation marker required.
  {
    const td::string payload(131072u, 'Y');
    capture.messages.clear();
    {
      td::Logger logger(capture, options_no_nl, VERBOSITY_NAME(ERROR));
      logger << payload;
    }
    ASSERT_TRUE(!capture.messages.empty());
    ASSERT_TRUE(capture.messages.back().find("[truncated]") != td::string::npos);
  }
}

}  // namespace
