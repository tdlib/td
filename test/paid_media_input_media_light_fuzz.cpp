// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include "test/paid_media_input_media_test_utils.h"

#include <array>

namespace {

struct SnippetCase {
  int source_kind;
  td::string snippet;
  bool expected_present;
};

}  // namespace

TEST(PaidMediaInputMediaLightFuzz, DeterministicLiteralMatrixPreservesSharedPaidMediaInvariants) {
  const auto normalized_h = td::paid_media_input_media_test::normalized_message_content_h();
  const auto normalized_cpp = td::paid_media_input_media_test::normalized_message_content_cpp();
  const auto normalized_bcm = td::paid_media_input_media_test::normalized_business_connection_manager_cpp();

  const std::array<SnippetCase, 8> cases = {{
      {0,
       R"(telegram_api::object_ptr<telegram_api::InputMedia>get_message_content_input_media(constMessageContent*content,vector<telegram_api::object_ptr<telegram_api::InputMedia>>&&input_media);)",
       true},
      {0, R"(stringget_message_content_payload(constMessageContent*content);)", false},
      {1, R"(returnget_message_content_input_media(content,std::move(input_media));)", true},
      {1, R"(stringget_message_content_payload(constMessageContent*content){)", false},
      {1, R"(telegram_api::make_object<telegram_api::inputMediaPaidMedia>)", true},
      {2,
       R"(->send(std::move(message),get_message_content_input_media(message->content_.get(),std::move(input_media)));)",
       true},
      {2, R"(autopayload=get_message_content_payload(message->content_.get());)", false},
      {2, R"(telegram_api::make_object<telegram_api::inputMediaPaidMedia>)", false},
  }};

  for (int iteration = 0; iteration < 10000; iteration++) {
    const auto &test_case = cases[static_cast<size_t>(td::Random::fast(0, static_cast<int>(cases.size()) - 1))];
    const auto &source = test_case.source_kind == 0   ? normalized_h
                         : test_case.source_kind == 1 ? normalized_cpp
                                                      : normalized_bcm;
    ASSERT_EQ(test_case.expected_present, source.find(test_case.snippet) != td::string::npos);
  }
}