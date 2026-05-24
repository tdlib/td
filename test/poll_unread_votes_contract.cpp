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

TEST(PollUnreadVotesContract, UpdatePathClearsMessageStateBeforeCounterRemoval) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(
      source,
      "void MessagesManager::on_update_poll_has_unread_votes(MessageFullId message_full_id, bool has_unread_votes) {",
      "void MessagesManager::on_update_message_content(MessageFullId message_full_id) {");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("remove_message_unread_poll_votes(d,m,\"on_update_poll_has_unread_votes\");") !=
              td::string::npos);
  ASSERT_EQ(normalized.find("on_unread_poll_vote_removed(d,m,\"on_update_poll_has_unread_votes\");"), td::string::npos);
}

TEST(PollUnreadVotesContract, UpdatePathAvoidsDuplicateChatLevelFanoutEmission) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(
      source,
      "void MessagesManager::on_update_poll_has_unread_votes(MessageFullId message_full_id, bool has_unread_votes) {",
      "void MessagesManager::on_update_message_content(MessageFullId message_full_id) {");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("on_unread_poll_vote_added(d,m,\"on_update_poll_has_unread_votes\");") !=
              td::string::npos);
  ASSERT_EQ(normalized.find("send_update_chat_unread_poll_vote_count(d,\"on_update_poll_has_unread_votes\");"),
            td::string::npos);
}

TEST(PollUnreadVotesContract, UpdateMessagePathReliesOnPerMessageUnreadVoteEmitterOnly) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("if(!is_scheduled&&update_message_contains_unread_poll_votes(d,old_message,new_message->"
                              "contains_unread_poll_votes,\"update_message\")){need_send_update=true;") !=
              td::string::npos);
  ASSERT_EQ(normalized.find("send_update_chat_unread_poll_vote_count(d,\"update_message\");"), td::string::npos);
}

TEST(PollUnreadVotesContract, ReadAllLocalPathClearsCountersBeforeUnreadMessageLoop) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(source,
                               "void MessagesManager::read_all_local_dialog_poll_votes(DialogId dialog_id, "
                               "ForumTopicId forum_topic_id) {",
                               "void MessagesManager::read_all_dialog_poll_votes(DialogId dialog_id, ForumTopicId "
                               "forum_topic_id,");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("if(forum_topic_id.is_valid()){td_->forum_topic_manager_->on_topic_poll_vote_count_"
                              "changed(dialog_id,forum_topic_id,0,false);") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(d->unread_poll_vote_count!=0){set_dialog_unread_poll_vote_count(d,0);if(message_"
                              "ids.empty()){send_update_chat_unread_poll_vote_count(d,\"read_all_local_dialog_"
                              "poll_votes\");}") != td::string::npos);
  ASSERT_TRUE(normalized.find("remove_message_content_poll_has_unread_votes(td_,m->content.get());") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find("on_unread_poll_vote_removed(d,m,nullptr,skip_forum_topic_counter_update);") !=
              td::string::npos);
}

TEST(PollUnreadVotesContract,
     ForumTopicLocalReadSuppressesRelativeTopicDecrementAfterAbsoluteResetToPreventUnderflowLogs) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto local_read_region = extract_region(source,
                                          "void MessagesManager::read_all_local_dialog_poll_votes(DialogId dialog_id, "
                                          "ForumTopicId forum_topic_id) {",
                                          "void MessagesManager::read_all_dialog_poll_votes(DialogId dialog_id, "
                                          "ForumTopicId forum_topic_id,");
  auto removal_region = extract_region(
      source, "void MessagesManager::on_unread_poll_vote_removed(Dialog *d, const Message *m, const char *source,",
      "bool MessagesManager::remove_message_unread_poll_votes(Dialog *d, Message *m, const char *source) {");

  auto local_read_normalized = normalize_for_contract(local_read_region);
  auto removal_normalized = normalize_for_contract(removal_region);

  ASSERT_TRUE(local_read_normalized.find("on_topic_poll_vote_count_changed(dialog_id,forum_topic_id,0,false);") !=
              td::string::npos);
  ASSERT_TRUE(local_read_normalized.find("constboolskip_forum_topic_counter_update=forum_topic_id.is_valid();") !=
              td::string::npos);
  ASSERT_TRUE(local_read_normalized.find("on_unread_poll_vote_removed(d,m,nullptr,skip_forum_topic_counter_update);") !=
              td::string::npos);
  ASSERT_TRUE(
      removal_normalized.find("if(d->is_forum&&!skip_forum_topic_counter_update){td_->forum_topic_manager_->"
                              "on_topic_poll_vote_count_changed(d->dialog_id,get_message_forum_topic_id(d->dialog_id,"
                              "m),-1,true);") != td::string::npos);
  ASSERT_EQ(
      removal_normalized.find("if(d->is_forum){td_->forum_topic_manager_->on_topic_poll_vote_count_changed(d->dialog_"
                              "id,get_message_forum_topic_id(d->dialog_id,m),-1,true);"),
      td::string::npos);
}

TEST(PollUnreadVotesContract, ReadAllDialogPathDelegatesCounterResetToLocalHelperWithoutLegacyBranch) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(
      source, "void MessagesManager::read_all_dialog_poll_votes(DialogId dialog_id, ForumTopicId forum_topic_id,",
      "void MessagesManager::read_message_content_from_updates(MessageId message_id, int32 read_date) {");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("read_all_local_dialog_poll_votes(dialog_id,forum_topic_id);") != td::string::npos);
  ASSERT_EQ(normalized.find("autois_update_sent=read_all_local_dialog_poll_votes(dialog_id,forum_topic_id);"),
            td::string::npos);
  ASSERT_EQ(normalized.find("if(!is_update_sent){"), td::string::npos);
  ASSERT_EQ(normalized.find("send_update_chat_unread_poll_vote_count(d,\"read_all_dialog_poll_votes\");"),
            td::string::npos);
  ASSERT_EQ(normalized.find("on_dialog_updated(dialog_id,\"read_all_dialog_poll_votes\");"), td::string::npos);
  ASSERT_EQ(normalized.find("on_topic_poll_vote_count_changed(dialog_id,forum_topic_id,0,false);"), td::string::npos);
}

TEST(PollUnreadVotesContract, UnreadVoteRemovalNegativeLogMustBeGuardedWhenSourceIsNull) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(
      source, "void MessagesManager::on_unread_poll_vote_removed(Dialog *d, const Message *m, const char *source,",
      "bool MessagesManager::remove_message_unread_poll_votes(Dialog *d, Message *m, const char *source) {");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("if(is_dialog_inited(d)&&source!=nullptr){") != td::string::npos);
}

TEST(PollUnreadVotesContract, UnreadVoteRemovalZeroCountBranchStillEmitsPerMessageUpdateAndDialogRefresh) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(
      source, "void MessagesManager::on_unread_poll_vote_removed(Dialog *d, const Message *m, const char *source,",
      "bool MessagesManager::remove_message_unread_poll_votes(Dialog *d, Message *m, const char *source) {");
  auto normalized = normalize_for_contract(region);

  auto zero_branch_pos = normalized.find("if(d->unread_poll_vote_count==0){");
  auto decrement_branch_pos =
      normalized.find("}else{set_dialog_unread_poll_vote_count(d,d->unread_poll_vote_count-1);}", zero_branch_pos);
  auto message_update_pos =
      normalized.find("send_update_message_contains_unread_poll_votes(d->dialog_id,m,d->unread_poll_vote_count);",
                      decrement_branch_pos);
  auto dialog_update_pos =
      normalized.find("on_dialog_updated(d->dialog_id,\"on_unread_poll_vote_removed\");", message_update_pos);

  ASSERT_NE(zero_branch_pos, td::string::npos);
  ASSERT_NE(decrement_branch_pos, td::string::npos);
  ASSERT_NE(message_update_pos, td::string::npos);
  ASSERT_NE(dialog_update_pos, td::string::npos);
  ASSERT_TRUE(zero_branch_pos < decrement_branch_pos);
  ASSERT_TRUE(decrement_branch_pos < message_update_pos);
  ASSERT_TRUE(message_update_pos < dialog_update_pos);
}

TEST(PollUnreadVotesContract, UnreadPollVoteHelperFailClosesOnMissingMessageContent) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(source,
                               "bool MessagesManager::has_unread_poll_votes(DialogId dialog_id, const Message *m) "
                               "const {",
                               "void MessagesManager::on_message_reply_info_changed(DialogId dialog_id, const Message "
                               "*m) const {");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("if(is_message_forward(m)||m->content==nullptr||m->content->get_type()!="
                              "MessageContentType::Poll){returnfalse;") != td::string::npos);
}

TEST(PollUnreadVotesContract, UpdatePathFailClosesWhenDialogOrMessageIsMissing) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(
      source,
      "void MessagesManager::on_update_poll_has_unread_votes(MessageFullId message_full_id, bool has_unread_votes) {",
      "void MessagesManager::on_update_message_content(MessageFullId message_full_id) {");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("if(d==nullptr){") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(m==nullptr){") != td::string::npos);
}

TEST(PollUnreadVotesContract, PollManagerUnreadVoteNotifyPathKeepsBotGateAndServerMessageFanoutOnly) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/PollManager.cpp");
  auto region = extract_region(source,
                               "void PollManager::notify_on_poll_has_unread_votes_update(PollId poll_id, bool "
                               "has_unread_votes) {",
                               "string PollManager::get_poll_database_key(PollId poll_id) {");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("if(td_->auth_manager_->is_bot()){return;") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(server_poll_messages_.count(poll_id)>0){") != td::string::npos);
  ASSERT_TRUE(normalized.find("on_update_poll_has_unread_votes(message_full_id,has_unread_votes);") !=
              td::string::npos);
}

TEST(PollUnreadVotesContract, PollManagerSkipsUnreadVoteMutationWhileReadPollVotesQueryIsPending) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/PollManager.cpp");
  auto region = extract_region(
      source,
      "if (!is_min && !td_->auth_manager_->is_bot() && poll_results->has_unread_votes_ != poll->has_unread_votes_) {",
      "if (!is_min && poll_results->can_view_stats_ != poll->can_view_stats_) {");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("if(!is_min&&!td_->auth_manager_->is_bot()&&poll_results->has_unread_votes_!=poll->has_"
                              "unread_votes_){") != td::string::npos);
  ASSERT_TRUE(normalized.find("boolhas_pending_read_poll_votes=false;") != td::string::npos);
  ASSERT_TRUE(normalized.find("has_message_pending_read_poll_votes(message_full_id)") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(!has_pending_read_poll_votes){") != td::string::npos);
  ASSERT_TRUE(normalized.find("poll->has_unread_votes_=poll_results->has_unread_votes_;") != td::string::npos);
}

TEST(PollUnreadVotesContract, TdApiSchemaPinsUnreadPollVoteMessageStateAndUpdateContract) {
  auto source = td::mtproto::test::read_repo_text_file("td/generate/scheme/td_api.tl");

  ASSERT_TRUE(source.find("contains_unread_poll_votes:Bool") != td::string::npos);
  ASSERT_TRUE(source.find("updateMessageContainsUnreadPollVotes") != td::string::npos);
  ASSERT_TRUE(source.find("contains_unread_poll_votes:Bool unread_poll_vote_count:int32 = Update;") !=
              td::string::npos);
}

}  // namespace
