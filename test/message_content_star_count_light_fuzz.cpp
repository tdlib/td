// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include "test/message_content_star_count_test_utils.h"

#include <array>

namespace {

struct SnippetCase {
  bool use_manager;
  td::string snippet;
  bool expected_present;
};

}  // namespace

TEST(MessageContentStarCountLightFuzz, DeterministicLiteralMatrixPreservesHelperInvariants) {
  const auto normalized_content = td::message_content_star_count_test::normalized_star_count_function();
  const auto normalized_manager = td::message_content_star_count_test::normalized_invoice_message_info_block();

  const std::array<SnippetCase, 6> cases = {{
      {false, "default:return0;", true},
      {false, R"(caseMessageContentType::PaidMedia:returnstatic_cast<constMessagePaidMedia*>(content)->star_count;)",
       true},
      {false, R"(CHECK(content->get_type()==MessageContentType::PaidMedia);)", false},
      {true, R"(result.server_message_id_=m->message_id.get_server_message_id();)", true},
      {true, R"(result.star_count_=get_message_content_star_count(m->content.get());)", true},
      {true, R"(content_type!=MessageContentType::PaidMedia?0:get_message_content_star_count(m->content.get());)",
       false},
  }};

  for (int iteration = 0; iteration < 10000; iteration++) {
    const auto &test_case = cases[static_cast<size_t>(td::Random::fast(0, static_cast<int>(cases.size()) - 1))];
    const auto &source = test_case.use_manager ? normalized_manager : normalized_content;
    ASSERT_EQ(test_case.expected_present, source.find(test_case.snippet) != td::string::npos);
  }
}