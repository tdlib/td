// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/message_link_info_consumer_test_utils.h"

namespace {

TEST(MessageLinkInfoConsumerIntegration, IncomingAndOutgoingMessageLinkPathsShareChecklistAndPollValidationHelpers) {
  auto source = td::message_link_info_consumer_test::read_normalized_messages_manager();

  ASSERT_EQ(2u, td::message_link_info_consumer_test::count_occurrences(
                    source, "get_message_content_to_do_list_has_item(m->content.get(),"));
  ASSERT_EQ(2u, td::message_link_info_consumer_test::count_occurrences(
                    source, "get_message_content_poll_has_option(td_,m->content.get(),"));
}

TEST(MessageLinkInfoConsumerIntegration, GeneratorUsesValidatedSelectorGuardsInCommentAndCanonicalLinkPaths) {
  auto region = td::message_link_info_consumer_test::read_normalized_generator_region();

  ASSERT_TRUE(region.contains("if(has_valid_linked_task){sb<<\"&task=\"<<todo_item_id;}"));
  ASSERT_TRUE(region.contains("if(has_valid_linked_poll_option){sb<<\"&option=\"<<base64url_encode(poll_option_id);}"));
  ASSERT_TRUE(region.contains("if(has_valid_linked_task){sb<<separator<<\"task=\"<<todo_item_id;separator='&';}"));
  ASSERT_TRUE(
      region.contains("if(has_valid_linked_poll_option){sb<<separator<<\"option=\"<<base64url_encode(poll_option_id);"
                      "separator='&';}"));
}

TEST(MessageLinkInfoConsumerIntegration, WebPagesManagerKeepsIndependentFailClosedTimestampExtraction) {
  auto region = td::message_link_info_consumer_test::read_normalized_video_timestamp_region();

  ASSERT_TRUE(region.contains("autor_info=LinkManager::get_message_link_info(url);"));
  ASSERT_TRUE(region.contains("if(r_info.is_error()){return0;}"));
}

}  // namespace