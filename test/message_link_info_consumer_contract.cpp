// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs

#include "td/utils/tests.h"

#include "test/message_link_info_consumer_test_utils.h"

namespace {

TEST(MessageLinkInfoConsumerContract, MessageContentDeclaresIdentifierValidationHelpers) {
  auto source = td::message_link_info_consumer_test::read_message_content_header();

  ASSERT_TRUE(source.find("bool get_message_content_poll_has_option(") != td::string::npos);
  ASSERT_TRUE(source.find("bool get_message_content_to_do_list_has_item(") != td::string::npos);
}

TEST(MessageLinkInfoConsumerContract, MessagesManagerFiltersIdentifiersByResolvedMessageContent) {
  auto region = td::message_link_info_consumer_test::read_normalized_consumer_region();

  ASSERT_TRUE(region.contains("get_message_content_to_do_list_has_item(m->content.get(),info.todo_item_id)"));
  ASSERT_TRUE(region.contains("get_message_content_poll_has_option(td_,m->content.get(),info.poll_option_id)"));
}

TEST(MessageLinkInfoConsumerContract, MessagesManagerGetMessageLinkDeclaresValidatedSelectorGuards) {
  auto region = td::message_link_info_consumer_test::read_normalized_generator_region();

  ASSERT_TRUE(region.contains(
      "boolhas_valid_linked_task=todo_item_id!=0&&get_message_content_to_do_list_has_item(m->content.get(),"
      "todo_item_id);"));
  ASSERT_TRUE(region.contains(
      "boolhas_valid_linked_poll_option=!poll_option_id.empty()&&get_message_content_poll_has_option(td_,m->"
      "content.get(),poll_option_id);"));
}

TEST(MessageLinkInfoConsumerContract, WebPagesManagerVideoTimestampPathStaysFailClosedOnParseErrors) {
  auto region = td::message_link_info_consumer_test::read_normalized_video_timestamp_region();

  ASSERT_TRUE(region.contains("if(r_info.is_error()){return0;}"));
  ASSERT_TRUE(region.contains("returnr_info.ok().media_timestamp;"));
}

}  // namespace