// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <string_view>

namespace {

td::string extract_region(std::string_view source, td::Slice begin_marker, td::Slice end_marker) {
  auto begin = source.find(begin_marker.str());
  CHECK(begin != td::string::npos);
  auto end = source.find(end_marker.str(), begin + begin_marker.size());
  CHECK(end != td::string::npos);
  CHECK(end > begin);
  return td::string(source.substr(begin, end - begin));
}

TEST(PollUnreadVotesIntegration, MessageObjectAndUpdateEmitterShareUnreadPollVoteState) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto object_region = extract_region(
      source,
      "td_api::object_ptr<td_api::message> MessagesManager::get_message_object(DialogId dialog_id, const Message *m,",
      "td_api::object_ptr<td_api::messages> MessagesManager::get_messages_object(int32 total_count, DialogId "
      "dialog_id,");

  ASSERT_TRUE(object_region.find("has_unread_poll_votes(dialog_id, m)") != td::string::npos);
  ASSERT_TRUE(source.find("void MessagesManager::send_update_message_contains_unread_poll_votes(") != td::string::npos);
  ASSERT_TRUE(source.find("updateMessageContainsUnreadPollVotes") != td::string::npos);
}

TEST(PollUnreadVotesIntegration, ReadAllPollVotesClearsMessageFlagsBeforeChatCounterUpdate) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(
      source, "void MessagesManager::read_all_dialog_poll_votes(DialogId dialog_id, ForumTopicId forum_topic_id,",
      "void MessagesManager::read_message_content_from_updates(MessageId message_id, int32 read_date) {");

  auto local_clear_pos =
      region.find("auto is_update_sent = read_all_local_dialog_poll_votes(dialog_id, forum_topic_id);");
  auto conditional_branch_pos = region.find("if (!is_update_sent) {");
  auto chat_counter_pos =
      region.find("send_update_chat_unread_poll_vote_count(d, \"read_all_dialog_poll_votes\")", conditional_branch_pos);
  auto on_dialog_updated_pos =
      region.find("on_dialog_updated(dialog_id, \"read_all_dialog_poll_votes\")", conditional_branch_pos);

  ASSERT_NE(local_clear_pos, td::string::npos);
  ASSERT_NE(conditional_branch_pos, td::string::npos);
  ASSERT_NE(chat_counter_pos, td::string::npos);
  ASSERT_NE(on_dialog_updated_pos, td::string::npos);
  ASSERT_TRUE(local_clear_pos < conditional_branch_pos);
  ASSERT_TRUE(conditional_branch_pos < chat_counter_pos);
  ASSERT_TRUE(conditional_branch_pos < on_dialog_updated_pos);
}

}  // namespace
