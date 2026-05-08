// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

// Precise truncation-threshold contract tests.
//
// Logger::BUFFER_SIZE = 128 * 1024 = 131072 bytes.
// StringBuilder::RESERVED_SIZE = 30 bytes (private, derived empirically).
// effective end_ptr_ = buffer.begin + BUFFER_SIZE - RESERVED_SIZE
//                    = buffer.begin + 131042
//
// With fix_newlines=true (destructor appends '\n'):
//   '\n' fits when current_ptr < end_ptr_, i.e. wrote < 131042 bytes.
//   '\n' overflows when wrote >= 131042 → is_error() set → truncation marker.
//   Threshold: payload_size >= 131042 (== BUFFER_SIZE - RESERVED_SIZE).
//
// With fix_newlines=false (destructor does NOT append '\n'):
//   is_error() set only when payload itself overflows available_size=131071.
//   Threshold: payload_size >= 131072 (== BUFFER_SIZE - RESERVED_SIZE + 1).
//
// These constants are treated as an implementation-contract boundary:
//   kTruncThreshFixNewlines     = 128 * 1024 - 30   = 131042
//   kTruncThreshNoFixNewlines   = 128 * 1024         = 131072

#include "td/utils/logging.h"
#include "td/utils/tests.h"

namespace {

// Logger BUFFER_SIZE (128 KiB) and StringBuilder RESERVED_SIZE (30).
// If either changes, these tests will fire and require the boundary to be
// re-derived and re-validated.
constexpr size_t kLoggerBufSize = 128u * 1024u;  // 131072
constexpr size_t kSbReservedSize = 30u;

// Threshold at which the trailing '\n' appended by fix_newlines=true will
// overflow the buffer, triggering is_error() and the truncation marker.
constexpr size_t kTruncThreshFixNewlines = kLoggerBufSize - kSbReservedSize;  // 131042

// Threshold at which the payload itself overflows available_size when
// fix_newlines=false.  available_size = end_ptr + RESERVED_SIZE - 1 - begin
//                                     = (BUFFER_SIZE - RESERVED_SIZE) + RESERVED_SIZE - 1
//                                     = BUFFER_SIZE - 1 = 131071.
// So threshold is BUFFER_SIZE = 131072.
constexpr size_t kTruncThreshNoFixNewlines = kLoggerBufSize;  // 131072

class CapturingLog final : public td::LogInterface {
 public:
  void do_append(int log_level, td::CSlice slice) final {
    (void)log_level;
    last_message = slice.str();
    invocations++;
  }

  td::string last_message;
  int invocations{0};
};

// ---------------------------------------------------------------------------
// fix_newlines=true boundary tests
// ---------------------------------------------------------------------------

// Payload one byte BELOW threshold: exactly 131041 bytes.
// '\n' still fits; no overflow; no truncation marker.
TEST(LoggingTruncationBoundaryContract, FixNewlinesTrueOneBelowThresholdHasNoTruncationMarker) {
  CapturingLog capture;
  td::LogOptions options(VERBOSITY_NAME(DEBUG), /*fix_newlines=*/true, /*add_info=*/false);

  const size_t payload_size = kTruncThreshFixNewlines - 1;  // 131041
  const td::string payload(payload_size, 'A');
  {
    td::Logger logger(capture, options, VERBOSITY_NAME(ERROR));
    logger << payload;
  }

  ASSERT_EQ(1, capture.invocations);
  ASSERT_TRUE(!capture.last_message.empty());
  ASSERT_EQ('\n', capture.last_message.back());
  // No truncation marker: the message was NOT overflowed
  ASSERT_TRUE(capture.last_message.find("[truncated]") == td::string::npos);
}

// Payload exactly AT threshold: 131042 bytes.
// '\n' cannot fit; is_error() set; truncation marker must appear.
TEST(LoggingTruncationBoundaryContract, FixNewlinesTrueAtThresholdHasTruncationMarker) {
  CapturingLog capture;
  td::LogOptions options(VERBOSITY_NAME(DEBUG), /*fix_newlines=*/true, /*add_info=*/false);

  const size_t payload_size = kTruncThreshFixNewlines;  // 131042
  const td::string payload(payload_size, 'B');
  {
    td::Logger logger(capture, options, VERBOSITY_NAME(ERROR));
    logger << payload;
  }

  ASSERT_EQ(1, capture.invocations);
  ASSERT_TRUE(!capture.last_message.empty());
  ASSERT_EQ('\n', capture.last_message.back());
  ASSERT_TRUE(capture.last_message.find("[truncated]") != td::string::npos);
}

// Payload in the OLD incorrect zone [131042, 131071] — should all have marker.
// Previously the tests used threshold 131072, missing this entire band.
TEST(LoggingTruncationBoundaryContract, FixNewlinesTrueMissingBandAllHaveTruncationMarker) {
  CapturingLog capture;
  td::LogOptions options(VERBOSITY_NAME(DEBUG), /*fix_newlines=*/true, /*add_info=*/false);

  // 30 payload sizes in the gap that the old threshold missed
  for (size_t sz = kTruncThreshFixNewlines; sz < kTruncThreshNoFixNewlines; sz++) {
    capture.last_message.clear();
    capture.invocations = 0;

    const td::string payload(sz, static_cast<char>('A' + (sz % 26)));
    {
      td::Logger logger(capture, options, VERBOSITY_NAME(ERROR));
      logger << payload;
    }

    ASSERT_EQ(1, capture.invocations);
    ASSERT_TRUE(!capture.last_message.empty());
    ASSERT_EQ('\n', capture.last_message.back());
    ASSERT_TRUE(capture.last_message.find("[truncated]") != td::string::npos);
  }
}

// ---------------------------------------------------------------------------
// fix_newlines=false boundary tests
// ---------------------------------------------------------------------------

// fix_newlines=false, payload = 131071: fits entirely; is_error() stays false.
TEST(LoggingTruncationBoundaryContract, FixNewlinesFalseOneBelowThresholdHasNoTruncationMarker) {
  CapturingLog capture;
  td::LogOptions options(VERBOSITY_NAME(DEBUG), /*fix_newlines=*/false, /*add_info=*/false);

  const size_t payload_size = kTruncThreshNoFixNewlines - 1;  // 131071
  const td::string payload(payload_size, 'C');
  {
    td::Logger logger(capture, options, VERBOSITY_NAME(ERROR));
    logger << payload;
  }

  ASSERT_EQ(1, capture.invocations);
  ASSERT_TRUE(!capture.last_message.empty());
  ASSERT_TRUE(capture.last_message.find("[truncated]") == td::string::npos);
}

// fix_newlines=false, payload = 131072: overflows available_size=131071.
// is_error() set; truncation marker must appear.
TEST(LoggingTruncationBoundaryContract, FixNewlinesFalseAtThresholdHasTruncationMarker) {
  CapturingLog capture;
  td::LogOptions options(VERBOSITY_NAME(DEBUG), /*fix_newlines=*/false, /*add_info=*/false);

  const size_t payload_size = kTruncThreshNoFixNewlines;  // 131072
  const td::string payload(payload_size, 'D');
  {
    td::Logger logger(capture, options, VERBOSITY_NAME(ERROR));
    logger << payload;
  }

  ASSERT_EQ(1, capture.invocations);
  ASSERT_TRUE(!capture.last_message.empty());
  ASSERT_TRUE(capture.last_message.find("[truncated]") != td::string::npos);
}

// Both mode thresholds must yield non-empty messages — no silent drops.
TEST(LoggingTruncationBoundaryContract, TruncatedMessagesAreNeverSilentlyDropped) {
  CapturingLog capture;

  const td::vector<std::pair<bool, size_t>> cases = {
      {true, kTruncThreshFixNewlines},      // fix_newlines=true, at threshold
      {true, kTruncThreshFixNewlines + 1},  // fix_newlines=true, above threshold
      {false, kTruncThreshNoFixNewlines},   // fix_newlines=false, at threshold
      {false, kTruncThreshNoFixNewlines + 1},
  };

  for (const auto &[fix_newlines, sz] : cases) {
    capture.last_message.clear();
    capture.invocations = 0;

    td::LogOptions options(VERBOSITY_NAME(DEBUG), fix_newlines, /*add_info=*/false);
    const td::string payload(sz, 'E');
    {
      td::Logger logger(capture, options, VERBOSITY_NAME(ERROR));
      logger << payload;
    }

    ASSERT_EQ(1, capture.invocations);
    ASSERT_TRUE(!capture.last_message.empty());
    ASSERT_TRUE(capture.last_message.find("[truncated]") != td::string::npos);
  }
}

// Marker appears exactly once, never duplicated, regardless of overflow depth.
TEST(LoggingTruncationBoundaryContract, TruncationMarkerAppearsExactlyOnceForBothPaths) {
  CapturingLog capture;

  const td::vector<std::pair<bool, size_t>> cases = {
      {true, kTruncThreshFixNewlines},    {true, kTruncThreshFixNewlines + 29},
      {true, kTruncThreshNoFixNewlines},  {true, kTruncThreshNoFixNewlines + 4096},
      {false, kTruncThreshNoFixNewlines}, {false, kTruncThreshNoFixNewlines + 4096},
  };

  for (const auto &[fix_newlines, sz] : cases) {
    capture.last_message.clear();
    capture.invocations = 0;

    td::LogOptions options(VERBOSITY_NAME(DEBUG), fix_newlines, /*add_info=*/false);
    const td::string payload(sz, 'F');
    {
      td::Logger logger(capture, options, VERBOSITY_NAME(ERROR));
      logger << payload;
    }

    ASSERT_EQ(1, capture.invocations);

    size_t count = 0;
    size_t pos = 0;
    while (true) {
      pos = capture.last_message.find("[truncated]", pos);
      if (pos == td::string::npos) {
        break;
      }
      count++;
      pos += 11;  // len("[truncated]")
    }
    ASSERT_EQ(1u, count);
  }
}

// Content before truncation point is not corrupted: bytes before marker_start
// must match the original payload prefix.
TEST(LoggingTruncationBoundaryContract, ContentBeforeMarkerMatchesPayloadPrefix) {
  CapturingLog capture;
  td::LogOptions options(VERBOSITY_NAME(DEBUG), /*fix_newlines=*/true, /*add_info=*/false);

  const size_t payload_size = kTruncThreshFixNewlines + 512;
  // Use a payload of distinct repeated chars (easy to inspect)
  const td::string payload(payload_size, 'Z');
  {
    td::Logger logger(capture, options, VERBOSITY_NAME(ERROR));
    logger << payload;
  }

  ASSERT_TRUE(capture.last_message.find("[truncated]") != td::string::npos);

  // Find marker position and verify preceding content contains 'Z'
  auto marker_pos = capture.last_message.find("[truncated]");
  ASSERT_TRUE(marker_pos > 0u);
  // All bytes before the marker (index 0..marker_pos-1) should be 'Z'
  bool all_match = true;
  for (size_t i = 0; i < marker_pos; i++) {
    if (capture.last_message[i] != 'Z') {
      all_match = false;
      break;
    }
  }
  ASSERT_TRUE(all_match);
}

}  // namespace
