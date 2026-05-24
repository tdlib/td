// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/message_link_info_consumer_test_utils.h"

namespace {

TEST(MessageLinkInfoConsumerStress, RepeatedSourceReadsKeepValidatedSelectorRoutingStable) {
  constexpr int kIterations = 2500;
  td::uint32 checksum = 0;

  for (int i = 0; i < kIterations; i++) {
    auto message_content_header = td::message_link_info_consumer_test::read_message_content_header();
    auto messages_manager = td::message_link_info_consumer_test::read_normalized_messages_manager();
    auto generator_region = td::message_link_info_consumer_test::read_normalized_generator_region();
    auto consumer_region = td::message_link_info_consumer_test::read_normalized_consumer_region();
    auto web_pages_region = td::message_link_info_consumer_test::read_normalized_video_timestamp_region();
    auto poll_manager_header = td::message_link_info_consumer_test::read_normalized("td/telegram/PollManager.h");
    auto poll_manager_source = td::message_link_info_consumer_test::read_normalized("td/telegram/PollManager.cpp");
    auto to_do_list_header = td::message_link_info_consumer_test::read_normalized("td/telegram/ToDoList.h");
    auto to_do_list_source = td::message_link_info_consumer_test::read_normalized("td/telegram/ToDoList.cpp");
    auto to_do_item_header = td::message_link_info_consumer_test::read_normalized("td/telegram/ToDoItem.h");

    ASSERT_TRUE(message_content_header.contains("bool get_message_content_poll_has_option("));
    ASSERT_TRUE(message_content_header.contains("bool get_message_content_to_do_list_has_item("));
    ASSERT_TRUE(poll_manager_header.contains("boolhas_poll_option(PollIdpoll_id,conststring&option_id)const;"));
    ASSERT_TRUE(
        poll_manager_source.contains("boolPollManager::has_poll_option(PollIdpoll_id,conststring&option_id)const{"));
    ASSERT_TRUE(to_do_list_header.contains("boolhas_item_id(int32item_id)const;"));
    ASSERT_TRUE(to_do_list_source.contains("boolToDoList::has_item_id(int32item_id)const{"));
    ASSERT_TRUE(to_do_item_header.contains("int32get_id()const{returnid_;}"));

    ASSERT_TRUE(
        consumer_region.contains("get_message_content_to_do_list_has_item(m->content.get(),info.todo_item_id)"));
    ASSERT_TRUE(
        consumer_region.contains("get_message_content_poll_has_option(td_,m->content.get(),info.poll_option_id)"));
    ASSERT_TRUE(generator_region.contains(
        "boolhas_valid_linked_task=todo_item_id!=0&&get_message_content_to_do_list_has_item(m->content.get(),"
        "todo_item_id);"));
    ASSERT_TRUE(generator_region.contains(
        "boolhas_valid_linked_poll_option=!poll_option_id.empty()&&get_message_content_poll_has_option(td_,m->"
        "content.get(),poll_option_id);"));
    ASSERT_TRUE(generator_region.contains("if(has_valid_linked_task){sb<<\"&task=\"<<todo_item_id;}"));
    ASSERT_TRUE(generator_region.contains(
        "if(has_valid_linked_poll_option){sb<<\"&option=\"<<base64url_encode(poll_option_id);}"));
    ASSERT_TRUE(
        generator_region.contains("if(has_valid_linked_task){sb<<separator<<\"task=\"<<todo_item_id;separator='&';}"));
    ASSERT_TRUE(generator_region.contains(
        "if(has_valid_linked_poll_option){sb<<separator<<\"option=\"<<base64url_encode(poll_option_id);"
        "separator='&';}"));
    ASSERT_FALSE(generator_region.contains("if(todo_item_id!=0){sb<<\"&task=\"<<todo_item_id;}"));
    ASSERT_FALSE(
        generator_region.contains("if(!poll_option_id.empty()){sb<<\"&option=\"<<base64url_encode(poll_option_id);}"));
    ASSERT_FALSE(
        generator_region.contains("if(todo_item_id!=0){sb<<separator<<\"task=\"<<todo_item_id;separator='&';}"));
    ASSERT_FALSE(generator_region.contains(
        "if(!poll_option_id.empty()){sb<<separator<<\"option=\"<<base64url_encode(poll_option_id);separator='&';}"));
    ASSERT_EQ(2u, td::message_link_info_consumer_test::count_occurrences(
                      messages_manager, "get_message_content_to_do_list_has_item(m->content.get(),"));
    ASSERT_EQ(2u, td::message_link_info_consumer_test::count_occurrences(
                      messages_manager, "get_message_content_poll_has_option(td_,m->content.get(),"));
    ASSERT_TRUE(web_pages_region.contains("if(r_info.is_error()){return0;}"));
    ASSERT_TRUE(web_pages_region.contains("returnr_info.ok().media_timestamp;"));

    checksum += static_cast<td::uint32>(
        message_content_header.size() + messages_manager.size() + generator_region.size() + consumer_region.size() +
        web_pages_region.size() + poll_manager_header.size() + poll_manager_source.size() + to_do_list_header.size() +
        to_do_list_source.size() + to_do_item_header.size() + static_cast<size_t>(i));
  }

  ASSERT_TRUE(checksum != 0);
}

}  // namespace