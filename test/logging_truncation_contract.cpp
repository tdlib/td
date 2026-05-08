// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/utils/logging.h"
#include "td/utils/tests.h"

namespace {

using td::logging_hardening::test::count_occurrences;
using td::logging_hardening::test::load_repo_text;

class CapturingLog final : public td::LogInterface {
 public:
  void do_append(int log_level, td::CSlice slice) final {
    last_level = log_level;
    last_message = slice.str();
  }

  int last_level{VERBOSITY_NAME(NEVER)};
  td::string last_message;
};

TEST(LoggingTruncationContract, SourcePinsExplicitTruncationMarkerPolicy) {
  auto source = load_repo_text("tdutils/td/utils/logging.cpp");

  ASSERT_TRUE(source.find("kLogTruncationMarker") != td::string::npos);
  ASSERT_TRUE(source.find("sb_.is_error()") != td::string::npos);
  ASSERT_TRUE(source.find("append_truncation_marker") != td::string::npos);
}

TEST(LoggingTruncationContract, OverflowPathAppendsDeterministicMarker) {
  CapturingLog capture;
  td::LogOptions options(VERBOSITY_NAME(DEBUG), true, false);

  const td::string payload(300000, 'A');
  {
    td::Logger logger(capture, options, VERBOSITY_NAME(ERROR));
    logger << payload;
  }

  ASSERT_EQ(VERBOSITY_NAME(ERROR), capture.last_level);
  ASSERT_TRUE(!capture.last_message.empty());
  ASSERT_EQ('\n', capture.last_message.back());
  ASSERT_TRUE(capture.last_message.find("[truncated]") != td::string::npos);
}

TEST(LoggingTruncationContract, MarkerAppearsExactlyOnceOnOverflow) {
  CapturingLog capture;
  td::LogOptions options(VERBOSITY_NAME(DEBUG), true, false);

  const td::string payload(400000, 'B');
  {
    td::Logger logger(capture, options, VERBOSITY_NAME(ERROR));
    logger << payload;
  }

  ASSERT_EQ(1u, count_occurrences(capture.last_message, "[truncated]"));
}

}  // namespace
