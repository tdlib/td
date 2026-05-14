// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/paid_media_input_media_test_utils.h"

TEST(PaidMediaInputMediaStress, RepeatedSourceReadsKeepSharedPaidMediaHelperStable) {
  constexpr int kIterations = 3000;

  for (int iteration = 0; iteration < kIterations; iteration++) {
    auto normalized_h = td::paid_media_input_media_test::normalized_message_content_h();
    auto normalized_cpp = td::paid_media_input_media_test::normalized_message_content_cpp();
    auto normalized_bcm = td::paid_media_input_media_test::normalized_business_connection_manager_cpp();

    ASSERT_EQ(
        1u,
        td::paid_media_input_media_test::count_occurrences(
            normalized_h,
            R"(telegram_api::object_ptr<telegram_api::InputMedia>get_message_content_input_media(constMessageContent*content,vector<telegram_api::object_ptr<telegram_api::InputMedia>>&&input_media);)"));
    ASSERT_EQ(1u, td::paid_media_input_media_test::count_occurrences(
                      normalized_cpp, R"(returnget_message_content_input_media(content,std::move(input_media));)"));
    ASSERT_EQ(
        1u,
        td::paid_media_input_media_test::count_occurrences(
            normalized_bcm,
            R"(->send(std::move(message),get_message_content_input_media(message->content_.get(),std::move(input_media)));)"));
  }
}