// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/message_content_star_count_test_utils.h"

TEST(MessageContentStarCountIntegration, HelperAndInvoiceCallerTravelTogether) {
  auto normalized_content = td::message_content_star_count_test::normalized_star_count_function();
  auto normalized_manager = td::message_content_star_count_test::normalized_invoice_message_info_block();

  ASSERT_TRUE(normalized_content.find("default:return0;") != td::string::npos);
  ASSERT_TRUE(normalized_manager.find(R"(result.star_count_=get_message_content_star_count(m->content.get());)") !=
              td::string::npos);
}

TEST(MessageContentStarCountIntegration, PaidMediaHelperStillExposesUnderlyingField) {
  auto source = td::message_content_star_count_test::read_message_content_cpp();

  ASSERT_TRUE(source.find("return static_cast<const MessagePaidMedia *>(content)->star_count;") != td::string::npos);
}