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

td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    if (auto byte = static_cast<unsigned char>(c); byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
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

TEST(PollUnreadVotesIntegration, ReadAllLocalPollVotesClearsCounterOwnershipBeforeUnreadFlagLoop) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(source,
                               "void MessagesManager::read_all_local_dialog_poll_votes(DialogId dialog_id, "
                               "ForumTopicId forum_topic_id) {",
                               "void MessagesManager::read_all_dialog_poll_votes(DialogId dialog_id, ForumTopicId "
                               "forum_topic_id,");
  auto normalized = normalize_for_contract(region);

  auto set_counter_pos = normalized.find("set_dialog_unread_poll_vote_count(d,0);");
  auto remove_content_pos = normalized.find("remove_message_content_poll_has_unread_votes(td_,m->content.get());");
  auto notify_removed_pos =
      normalized.find("on_unread_poll_vote_removed(d,m,nullptr,skip_forum_topic_counter_update);", remove_content_pos);

  ASSERT_NE(set_counter_pos, td::string::npos);
  ASSERT_NE(remove_content_pos, td::string::npos);
  ASSERT_NE(notify_removed_pos, td::string::npos);
  ASSERT_TRUE(set_counter_pos < remove_content_pos);
  ASSERT_TRUE(remove_content_pos < notify_removed_pos);
  ASSERT_TRUE(normalized.find("on_topic_poll_vote_count_changed(dialog_id,forum_topic_id,0,false);") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find("send_update_chat_unread_poll_vote_count(d,\"read_all_local_dialog_poll_votes\");") !=
              td::string::npos);
}

TEST(PollUnreadVotesIntegration, ReadAllLocalPollVotesKeepsPerMessageFanoutAliveAfterDialogCounterIsPrecleared) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto helper_region = extract_region(source,
                                      "void MessagesManager::read_all_local_dialog_poll_votes(DialogId dialog_id, "
                                      "ForumTopicId forum_topic_id) {",
                                      "void MessagesManager::read_all_dialog_poll_votes(DialogId dialog_id, "
                                      "ForumTopicId forum_topic_id,");
  auto removal_region = extract_region(
      source, "void MessagesManager::on_unread_poll_vote_removed(Dialog *d, const Message *m, const char *source,",
      "bool MessagesManager::remove_message_unread_poll_votes(Dialog *d, Message *m, const char *source) {");

  auto helper_normalized = normalize_for_contract(helper_region);
  auto removal_normalized = normalize_for_contract(removal_region);

  ASSERT_TRUE(helper_normalized.find("set_dialog_unread_poll_vote_count(d,0);") != td::string::npos);
  ASSERT_TRUE(helper_normalized.find("on_unread_poll_vote_removed(d,m,nullptr,skip_forum_topic_counter_update);") !=
              td::string::npos);

  auto decrement_branch_pos =
      removal_normalized.find("}else{set_dialog_unread_poll_vote_count(d,d->unread_poll_vote_count-1);}");
  auto message_update_pos = removal_normalized.find(
      "send_update_message_contains_unread_poll_votes(d->dialog_id,m,d->unread_poll_vote_count);",
      decrement_branch_pos);
  auto dialog_update_pos =
      removal_normalized.find("on_dialog_updated(d->dialog_id,\"on_unread_poll_vote_removed\");", message_update_pos);

  ASSERT_NE(decrement_branch_pos, td::string::npos);
  ASSERT_NE(message_update_pos, td::string::npos);
  ASSERT_NE(dialog_update_pos, td::string::npos);
  ASSERT_TRUE(decrement_branch_pos < message_update_pos);
  ASSERT_TRUE(message_update_pos < dialog_update_pos);
}

TEST(PollUnreadVotesIntegration, ReadAllDialogPollVotesDelegatesSequencingToLocalHelperOnly) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(
      source, "void MessagesManager::read_all_dialog_poll_votes(DialogId dialog_id, ForumTopicId forum_topic_id,",
      "void MessagesManager::read_message_content_from_updates(MessageId message_id, int32 read_date) {");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("read_all_local_dialog_poll_votes(dialog_id,forum_topic_id);") != td::string::npos);
  ASSERT_TRUE(normalized.find("autois_update_sent=read_all_local_dialog_poll_votes(dialog_id,forum_topic_id);") ==
              td::string::npos);
  ASSERT_TRUE(normalized.find("if(!is_update_sent){") == td::string::npos);
  ASSERT_TRUE(normalized.find("send_update_chat_unread_poll_vote_count(d,\"read_all_dialog_poll_votes\");") ==
              td::string::npos);
  ASSERT_TRUE(normalized.find("on_dialog_updated(dialog_id,\"read_all_dialog_poll_votes\");") == td::string::npos);
  ASSERT_TRUE(normalized.find("on_topic_poll_vote_count_changed(dialog_id,forum_topic_id,0,false);") ==
              td::string::npos);
}

TEST(PollUnreadVotesIntegration, PollManagerUnreadVotePathIntegratesPendingReadTrackerFromMessageQueryManager) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/PollManager.cpp");

  ASSERT_TRUE(source.find("#include \"td/telegram/MessageQueryManager.h\"") != td::string::npos);

  auto unread_region = extract_region(
      source,
      "if (!is_min && !td_->auth_manager_->is_bot() && poll_results->has_unread_votes_ != poll->has_unread_votes_) {",
      "if (!is_min && poll_results->can_view_stats_ != poll->can_view_stats_) {");

  ASSERT_TRUE(unread_region.find("!td_->auth_manager_->is_bot()") != td::string::npos);
  ASSERT_TRUE(unread_region.find("has_message_pending_read_poll_votes") != td::string::npos);
  ASSERT_TRUE(unread_region.find("if (!has_pending_read_poll_votes)") != td::string::npos);
}

}  // namespace
