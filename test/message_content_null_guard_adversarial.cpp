// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/message_content_null_guard_test_utils.h"

using td::message_content_null_guard_test::extract_normalized_segment;
using td::message_content_null_guard_test::kGuardOrderExpectations;
using td::message_content_null_guard_test::normalized_message_content_cpp;

TEST(MessageContentNullGuardAdversarial, guard_precedes_first_content_dereference_in_high_risk_entry_points) {
  const auto source = normalized_message_content_cpp();

  for (const auto &expectation : kGuardOrderExpectations) {
    const auto region = extract_normalized_segment(source, expectation.begin_marker, expectation.end_marker);
    ASSERT_FALSE(region.empty());

    const auto guard_pos = region.find(expectation.guard_marker);
    const auto deref_pos = region.find(expectation.first_deref_marker);

    ASSERT_NE(td::string::npos, guard_pos);
    ASSERT_NE(td::string::npos, deref_pos);
    ASSERT_TRUE(guard_pos < deref_pos);
  }
}

TEST(MessageContentNullGuardAdversarial, legacy_naked_dereference_patterns_are_rejected) {
  const auto source = normalized_message_content_cpp();

  const char *forbidden_patterns[] = {
      "boolcan_forward_message_content(constTd*td,constMessageContent*content,boolis_copy){autocontent_type="
      "content->get_type();",
      "boolupdate_opened_message_content(MessageContent*content){switch(content->get_type())",
      "boolhas_message_content_web_page(constMessageContent*content){if(content->get_type()==MessageContentType::"
      "Text)",
      "voidregister_message_content(Td*td,constMessageContent*content,MessageFullIdmessage_full_id,int32"
      "message_date,constchar*source){autocontent_type=content->get_type();",
      "boolmerge_message_content_file_id(Td*td,MessageContent*message_content,FileIdnew_file_id){if(!new_file_id."
      "is_valid()){returnfalse;}LOG(INFO)<<\"Mergemessagecontentofamessagewithfile\"<<new_file_id;"
      "MessageContentTypecontent_type=message_content->get_type();",
  };

  for (const auto *forbidden : forbidden_patterns) {
    ASSERT_EQ(td::string::npos, source.find(forbidden));
  }
}
