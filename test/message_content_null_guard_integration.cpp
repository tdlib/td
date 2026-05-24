// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/message_content_null_guard_test_utils.h"

using td::message_content_null_guard_test::extract_normalized_segment;
using td::message_content_null_guard_test::kGuardExpectations;
using td::message_content_null_guard_test::normalized_message_content_cpp;

TEST(MessageContentNullGuardIntegration, non_null_guards_and_poll_fail_closed_paths_coexist) {
  const auto source = normalized_message_content_cpp();

  for (const auto &expectation : kGuardExpectations) {
    const auto region = extract_normalized_segment(source, expectation.begin_marker, expectation.end_marker);
    ASSERT_FALSE(region.empty());
    ASSERT_NE(td::string::npos, region.find(expectation.guard_marker));
  }

  ASSERT_NE(td::string::npos,
            source.find("PollIdget_message_content_poll_id(constMessageContent*content){if(content==nullptr){"
                        "returnPollId();}"));
  ASSERT_NE(td::string::npos,
            source.find("boolget_message_content_poll_is_closed(constTd*td,constMessageContent*content){if(content=="
                        "nullptr){returntrue;}"));
  ASSERT_NE(td::string::npos,
            source.find("voidremove_message_content_poll_has_unread_votes(Td*td,constMessageContent*content){if("
                        "content==nullptr){return;}"));
}

TEST(MessageContentNullGuardIntegration, type_checks_are_preserved_after_non_null_guards) {
  const auto source = normalized_message_content_cpp();

  const auto individual_region = extract_normalized_segment(
      source,
      "vector<unique_ptr<MessageContent>>get_individual_message_contents(constMessageContent"
      "*content){",
      "StickerTypeget_message_content_sticker_type(constTd*td,constMessageContent*content){");
  ASSERT_NE(td::string::npos, individual_region.find("CHECK(content!=nullptr);"));
  ASSERT_NE(td::string::npos, individual_region.find("CHECK(content->get_type()==MessageContentType::PaidMedia);"));

  const auto phone_call_region = extract_normalized_segment(
      source,
      "telegram_api::object_ptr<telegram_api::inputPhoneCall>get_message_content_input_phone_call(constMessageContent"
      "*content){",
      "int32get_message_content_live_location_period(constMessageContent*content){");
  ASSERT_NE(td::string::npos, phone_call_region.find("CHECK(content!=nullptr);"));
  ASSERT_NE(td::string::npos, phone_call_region.find("CHECK(content->get_type()==MessageContentType::Call);"));

  const auto remove_web_page_region =
      extract_normalized_segment(source, "voidremove_message_content_web_page(MessageContent*content){",
                                 "boolcan_message_content_have_media_timestamp(constMessageContent*content){");
  ASSERT_NE(td::string::npos, remove_web_page_region.find("CHECK(content!=nullptr);"));
  ASSERT_NE(td::string::npos, remove_web_page_region.find("CHECK(content->get_type()==MessageContentType::Text);"));
}
