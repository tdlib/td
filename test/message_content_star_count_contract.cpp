// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/message_content_star_count_test_utils.h"

TEST(MessageContentStarCountContract, HelperReturnsPaidMediaCountOrZero) {
  auto normalized = td::message_content_star_count_test::normalized_star_count_function();

  ASSERT_TRUE(
      normalized.find(
          R"(int64get_message_content_star_count(constMessageContent*content){switch(content->get_type()){caseMessageContentType::PaidMedia:returnstatic_cast<constMessagePaidMedia*>(content)->star_count;default:return0;}})") !=
      td::string::npos);
}

TEST(MessageContentStarCountContract, InvoiceMessageInfoUsesSharedHelperDirectly) {
  auto normalized = td::message_content_star_count_test::normalized_invoice_message_info_block();

  ASSERT_TRUE(normalized.find(R"(result.star_count_=get_message_content_star_count(m->content.get());)") !=
              td::string::npos);
}