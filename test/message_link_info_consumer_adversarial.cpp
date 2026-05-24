// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs

#include "td/utils/tests.h"

#include "test/message_link_info_consumer_test_utils.h"

namespace {

TEST(MessageLinkInfoConsumerAdversarial, MessagesManagerMustNotForwardIdentifiersUsingTypeCheckAlone) {
  auto region = td::message_link_info_consumer_test::read_normalized_consumer_region();

  ASSERT_FALSE(region.contains("if(info.todo_item_id>0&&m->content->get_type()==MessageContentType::ToDoList){"));
  ASSERT_FALSE(region.contains("if(!info.poll_option_id.empty()&&m->content->get_type()==MessageContentType::Poll){"));
}

TEST(MessageLinkInfoConsumerAdversarial, MessagesManagerGetMessageLinkMustNotAppendUncheckedTaskOrOptionSelectors) {
  auto region = td::message_link_info_consumer_test::read_normalized_generator_region();

  ASSERT_FALSE(region.contains("if(todo_item_id!=0){sb<<\"&task=\"<<todo_item_id;}"));
  ASSERT_FALSE(region.contains("if(!poll_option_id.empty()){sb<<\"&option=\"<<base64url_encode(poll_option_id);}"));
  ASSERT_FALSE(region.contains("if(todo_item_id!=0){sb<<separator<<\"task=\"<<todo_item_id;separator='&';}"));
  ASSERT_FALSE(region.contains(
      "if(!poll_option_id.empty()){sb<<separator<<\"option=\"<<base64url_encode(poll_option_id);separator='&';}"));
  ASSERT_TRUE(region.contains("if(has_valid_linked_task){"));
  ASSERT_TRUE(region.contains("if(has_valid_linked_poll_option){"));
}

}  // namespace