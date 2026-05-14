// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/paid_media_input_media_test_utils.h"

TEST(PaidMediaInputMediaAdversarial, BusinessConnectionManagerMustNotAssemblePaidMediaManually) {
  auto normalized = td::paid_media_input_media_test::normalized_business_connection_manager_cpp();

  ASSERT_TRUE(normalized.find(R"(autopayload=get_message_content_payload(message->content_.get());)") ==
              td::string::npos);
  ASSERT_TRUE(normalized.find(R"(telegram_api::make_object<telegram_api::inputMediaPaidMedia>)") == td::string::npos);
}

TEST(PaidMediaInputMediaAdversarial, MessageContentMustNotKeepPayloadAccessorDeclarationOrDefinition) {
  auto normalized_h = td::paid_media_input_media_test::normalized_message_content_h();
  auto normalized_cpp = td::paid_media_input_media_test::normalized_message_content_cpp();

  ASSERT_TRUE(normalized_h.find(R"(stringget_message_content_payload(constMessageContent*content);)") ==
              td::string::npos);
  ASSERT_TRUE(normalized_cpp.find(R"(stringget_message_content_payload(constMessageContent*content){)") ==
              td::string::npos);
}

TEST(PaidMediaInputMediaAdversarial, VectorOverloadMustContainSinglePaidMediaFactory) {
  auto normalized = td::paid_media_input_media_test::normalized_message_content_cpp();

  ASSERT_EQ(1u, td::paid_media_input_media_test::count_occurrences(
                    normalized, R"(telegram_api::make_object<telegram_api::inputMediaPaidMedia>)"));
}