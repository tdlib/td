// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include "test/message_content_null_guard_test_utils.h"

using td::message_content_null_guard_test::extract_normalized_segment;
using td::message_content_null_guard_test::kGuardExpectations;
using td::message_content_null_guard_test::kGuardOrderExpectations;
using td::message_content_null_guard_test::normalized_message_content_cpp;

TEST(MessageContentNullGuardLightFuzz, randomized_guard_sampling_detects_missing_or_misordered_guards) {
  const auto source = normalized_message_content_cpp();

  constexpr int kIterations = 12000;
  for (int i = 0; i < kIterations; i++) {
    const auto guard_index = static_cast<size_t>(td::Random::fast(0, static_cast<int>(kGuardExpectations.size()) - 1));
    const auto order_index =
        static_cast<size_t>(td::Random::fast(0, static_cast<int>(kGuardOrderExpectations.size()) - 1));

    const auto &guard_expectation = kGuardExpectations[guard_index];
    const auto guard_region =
        extract_normalized_segment(source, guard_expectation.begin_marker, guard_expectation.end_marker);
    ASSERT_FALSE(guard_region.empty());
    ASSERT_NE(td::string::npos, guard_region.find(guard_expectation.guard_marker));

    const auto &order_expectation = kGuardOrderExpectations[order_index];
    const auto order_region =
        extract_normalized_segment(source, order_expectation.begin_marker, order_expectation.end_marker);
    ASSERT_FALSE(order_region.empty());

    const auto guard_pos = order_region.find(order_expectation.guard_marker);
    const auto deref_pos = order_region.find(order_expectation.first_deref_marker);
    ASSERT_NE(td::string::npos, guard_pos);
    ASSERT_NE(td::string::npos, deref_pos);
    ASSERT_TRUE(guard_pos < deref_pos);
  }
}
