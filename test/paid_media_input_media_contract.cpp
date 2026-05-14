// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/paid_media_input_media_test_utils.h"

TEST(PaidMediaInputMediaContract, MessageContentHeaderDeclaresVectorInputMediaOverload) {
  auto normalized = td::paid_media_input_media_test::normalized_message_content_h();

  ASSERT_TRUE(
      normalized.find(
          R"(telegram_api::object_ptr<telegram_api::InputMedia>get_message_content_input_media(constMessageContent*content,vector<telegram_api::object_ptr<telegram_api::InputMedia>>&&input_media);)") !=
      td::string::npos);
}

TEST(PaidMediaInputMediaContract, PaidMediaBranchDelegatesToVectorOverload) {
  auto normalized = td::paid_media_input_media_test::normalized_message_content_cpp();

  ASSERT_TRUE(normalized.find(R"(returnget_message_content_input_media(content,std::move(input_media));)") !=
              td::string::npos);
}

TEST(PaidMediaInputMediaContract, BusinessConnectionManagerUsesSharedPaidMediaHelper) {
  auto normalized = td::paid_media_input_media_test::normalized_business_connection_manager_cpp();

  ASSERT_TRUE(
      normalized.find(
          R"(->send(std::move(message),get_message_content_input_media(message->content_.get(),std::move(input_media)));)") !=
      td::string::npos);
}