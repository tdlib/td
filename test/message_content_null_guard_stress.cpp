// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/message_content_null_guard_test_utils.h"

using td::message_content_null_guard_test::extract_normalized_segment;
using td::message_content_null_guard_test::kGuardExpectations;
using td::message_content_null_guard_test::normalized_message_content_cpp;

TEST(MessageContentNullGuardStress, repeated_source_reads_preserve_guard_and_fail_closed_invariants) {
  constexpr int kIterations = 2400;
  td::uint32 checksum = 0;

  for (int i = 0; i < kIterations; i++) {
    const auto source = normalized_message_content_cpp();

    for (const auto &expectation : kGuardExpectations) {
      const auto region = extract_normalized_segment(source, expectation.begin_marker, expectation.end_marker);
      ASSERT_FALSE(region.empty());
      ASSERT_NE(td::string::npos, region.find(expectation.guard_marker));
      checksum |= static_cast<td::uint32>(region.size() ^ static_cast<size_t>(i));
    }

    ASSERT_NE(td::string::npos,
              source.find("PollIdget_message_content_poll_id(constMessageContent*content){if(content==nullptr){"
                          "returnPollId();}"));
    ASSERT_NE(td::string::npos,
              source.find("boolget_message_content_poll_is_closed(constTd*td,constMessageContent*content){if(content=="
                          "nullptr){returntrue;}"));
  }

  ASSERT_TRUE(checksum != 0);
}
