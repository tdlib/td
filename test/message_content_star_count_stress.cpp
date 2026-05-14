// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/message_content_star_count_test_utils.h"

TEST(MessageContentStarCountStress, RepeatedSourceReadsKeepHelperContractStable) {
  constexpr int kIterations = 3000;

  for (int iteration = 0; iteration < kIterations; iteration++) {
    auto normalized_content = td::message_content_star_count_test::normalized_star_count_function();
    auto normalized_manager = td::message_content_star_count_test::normalized_invoice_message_info_block();

    ASSERT_EQ(1u, td::message_content_star_count_test::count_occurrences(normalized_content, "default:return0;"));
    ASSERT_EQ(1u, td::message_content_star_count_test::count_occurrences(
                      normalized_manager, R"(result.star_count_=get_message_content_star_count(m->content.get());)"));
  }
}