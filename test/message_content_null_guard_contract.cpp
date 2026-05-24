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

TEST(MessageContentNullGuardContract, targeted_entry_points_have_explicit_non_null_guards) {
  const auto source = normalized_message_content_cpp();

  for (const auto &expectation : kGuardExpectations) {
    const auto region = extract_normalized_segment(source, expectation.begin_marker, expectation.end_marker);
    ASSERT_FALSE(region.empty());
    ASSERT_NE(td::string::npos, region.find(expectation.guard_marker));
  }
}

TEST(MessageContentNullGuardContract, expected_number_of_hardened_entry_points_is_pinned) {
  constexpr size_t kExpectedGuardCount = 30;
  ASSERT_EQ(kExpectedGuardCount, kGuardExpectations.size());
}
