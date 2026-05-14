// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/message_content_star_count_test_utils.h"

TEST(MessageContentStarCountAdversarial, HelperMustNotHardCheckPaidMediaOnly) {
  auto normalized = td::message_content_star_count_test::normalized_star_count_function();

  ASSERT_TRUE(
      normalized.find(
          R"(int64get_message_content_star_count(constMessageContent*content){CHECK(content->get_type()==MessageContentType::PaidMedia);returnstatic_cast<constMessagePaidMedia*>(content)->star_count;})") ==
      td::string::npos);
}

TEST(MessageContentStarCountAdversarial, InvoicePathMustNotCarryLegacyPaidMediaTernary) {
  auto normalized = td::message_content_star_count_test::normalized_invoice_message_info_block();

  ASSERT_TRUE(
      normalized.find(
          R"(result.star_count_=content_type!=MessageContentType::PaidMedia?0:get_message_content_star_count(m->content.get());)") ==
      td::string::npos);
}

TEST(MessageContentStarCountAdversarial, HelperMustContainExactlyOneDefaultZeroReturn) {
  auto normalized = td::message_content_star_count_test::normalized_star_count_function();

  ASSERT_EQ(1u, td::message_content_star_count_test::count_occurrences(normalized, "default:return0;"));
}