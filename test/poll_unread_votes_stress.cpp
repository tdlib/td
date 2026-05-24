// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

namespace {

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

}  // namespace

TEST(PollUnreadVotesStress, RepeatedSourceReadsKeepUnreadPollVoteGuardsStable) {
  constexpr int kIterations = 2200;
  td::uint32 checksum = 0;

  for (int i = 0; i < kIterations; ++i) {
    auto messages_source =
        normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp"));
    auto poll_manager_source =
        normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/PollManager.cpp"));
    auto td_api_source = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/generate/scheme/td_api.tl"));

    ASSERT_NE(td::string::npos, messages_source.find("returnm->contains_unread_poll_votes;"));
    ASSERT_NE(td::string::npos,
              messages_source.find("constboolhas_current_unread_votes=has_unread_poll_votes(d->dialog_id,m);"));
    ASSERT_NE(td::string::npos,
              messages_source.find("switch(dispatch_poll_unread_votes_update_action(is_supported_poll_message,"
                                   "has_current_unread_votes,has_unread_votes)){"));
    ASSERT_NE(td::string::npos, messages_source.find("casePollUnreadVotesUpdateAction::IgnoredDuplicateState:"));
    ASSERT_NE(td::string::npos, messages_source.find("casePollUnreadVotesUpdateAction::RemovedUnreadVotes:"));
    ASSERT_NE(td::string::npos,
              messages_source.find("remove_message_unread_poll_votes(d,m,\"on_update_poll_has_unread_votes\");"));
    ASSERT_NE(td::string::npos, messages_source.find("m->contains_unread_poll_votes=true;"));
    ASSERT_NE(td::string::npos, messages_source.find("m->contains_unread_poll_votes=false;"));
    ASSERT_NE(td::string::npos,
              messages_source.find("remove_message_content_poll_has_unread_votes(td_,m->content.get());"));
    ASSERT_NE(td::string::npos,
              messages_source.find(
                  "send_update_message_contains_unread_poll_votes(d->dialog_id,m,d->unread_poll_vote_count);"));
    ASSERT_NE(
        td::string::npos,
        messages_source.find(
            "voidMessagesManager::read_all_local_dialog_poll_votes(DialogIddialog_id,ForumTopicIdforum_topic_id){"));
    ASSERT_NE(td::string::npos,
              messages_source.find("if(forum_topic_id.is_valid()){td_->forum_topic_manager_->on_topic_poll_vote_"
                                   "count_changed(dialog_id,forum_topic_id,0,false);}"));
    ASSERT_NE(td::string::npos,
              messages_source.find("if(d->unread_poll_vote_count!=0){set_dialog_unread_poll_vote_count(d,0);if"
                                   "(message_ids.empty()){send_update_chat_unread_poll_vote_count(d,\"read_all_"
                                   "local_dialog_poll_votes\");}}"));
    ASSERT_NE(td::string::npos, messages_source.find("read_all_local_dialog_poll_votes(dialog_id,forum_topic_id);"));
    ASSERT_NE(td::string::npos,
              messages_source.find("remove_message_content_poll_has_unread_votes(td_,m->content.get());"));
    ASSERT_NE(td::string::npos,
              messages_source.find("on_unread_poll_vote_removed(d,m,nullptr,skip_forum_topic_counter_update);"));
    ASSERT_NE(td::string::npos, messages_source.find("if(is_dialog_inited(d)&&source!=nullptr){"));
    ASSERT_NE(
        td::string::npos,
        messages_source.find("}else{set_dialog_unread_poll_vote_count(d,d->unread_poll_vote_count-1);}"
                             "send_update_message_contains_unread_poll_votes(d->dialog_id,m,d->unread_poll_vote_count);"
                             "on_dialog_updated(d->dialog_id,\"on_unread_poll_vote_removed\");"));
    ASSERT_NE(td::string::npos, poll_manager_source.find("boolhas_pending_read_poll_votes=false;"));
    ASSERT_NE(td::string::npos, poll_manager_source.find("has_message_pending_read_poll_votes(message_full_id)"));
    ASSERT_NE(td::string::npos,
              poll_manager_source.find("if(!has_pending_read_poll_votes){poll->has_unread_votes_=poll_results->"
                                       "has_unread_votes_"));

    ASSERT_NE(td::string::npos, td_api_source.find("contains_unread_poll_votes:Bool"));
    ASSERT_NE(td::string::npos,
              td_api_source.find("updateMessageContainsUnreadPollVoteschat_id:int53message_id:int53"
                                 "contains_unread_poll_votes:Boolunread_poll_vote_count:int32=Update;"));

    ASSERT_EQ(td::string::npos,
              messages_source.find("returnget_message_content_poll_has_unread_votes(td_,m->content.get());"));
    ASSERT_EQ(td::string::npos,
              messages_source.find("on_unread_poll_vote_removed(d,m,\"on_update_poll_has_unread_votes\");"));
    ASSERT_EQ(td::string::npos,
              messages_source.find("send_update_chat_unread_poll_vote_count(d,\"on_update_poll_has_unread_votes\");"));
    ASSERT_EQ(td::string::npos, messages_source.find("send_update_chat_unread_poll_vote_count(d,\"update_message\");"));
    ASSERT_EQ(
        td::string::npos,
        messages_source.find("}else{set_dialog_unread_poll_vote_count(d,d->unread_poll_vote_count-1);"
                             "send_update_message_contains_unread_poll_votes(d->dialog_id,m,d->unread_poll_vote_count);"
                             "on_dialog_updated(d->dialog_id,\"on_unread_poll_vote_removed\");}"));
    ASSERT_EQ(td::string::npos,
              messages_source.find("boolMessagesManager::read_all_local_dialog_poll_votes(DialogIddialog_id,"
                                   "ForumTopicIdforum_topic_id){"));
    ASSERT_EQ(td::string::npos,
              messages_source.find("autois_update_sent=read_all_local_dialog_poll_votes(dialog_id,forum_topic_id);"));
    ASSERT_EQ(td::string::npos,
              messages_source.find(
                  "if(!is_update_sent){send_update_chat_unread_poll_vote_count(d,\"read_all_dialog_poll_votes\");}"));
    ASSERT_EQ(td::string::npos, messages_source.find("on_dialog_updated(dialog_id,\"read_all_dialog_poll_votes\");"));
    ASSERT_EQ(td::string::npos, td_api_source.find("updateMessageUnreadPollVotes"));

    auto compare_pos = messages_source.find("constboolhas_current_unread_votes=has_unread_poll_votes(d->dialog_id,m);");
    auto idempotence_pos = messages_source.find("casePollUnreadVotesUpdateAction::IgnoredDuplicateState:");
    auto branch_pos = messages_source.find("casePollUnreadVotesUpdateAction::RemovedUnreadVotes:", idempotence_pos);
    auto remove_pos =
        messages_source.find("remove_message_unread_poll_votes(d,m,\"on_update_poll_has_unread_votes\");", branch_pos);
    auto add_pos = messages_source.find("m->contains_unread_poll_votes=true;", branch_pos);
    auto read_all_local_signature_pos = messages_source.find(
        "voidMessagesManager::read_all_local_dialog_poll_votes(DialogIddialog_id,"
        "ForumTopicIdforum_topic_id){");
    auto forum_topic_reset_pos = messages_source.find(
        "on_topic_poll_vote_count_changed(dialog_id,forum_topic_id,0,false);", read_all_local_signature_pos);
    auto local_counter_clear_pos =
        messages_source.find("set_dialog_unread_poll_vote_count(d,0);", forum_topic_reset_pos);
    auto local_remove_content_pos = messages_source.find(
        "remove_message_content_poll_has_unread_votes(td_,m->content.get());", local_counter_clear_pos);
    auto local_notify_removed_pos = messages_source.find(
        "on_unread_poll_vote_removed(d,m,nullptr,skip_forum_topic_counter_update);", local_remove_content_pos);
    auto read_all_dialog_delegate_pos =
        messages_source.find("read_all_local_dialog_poll_votes(dialog_id,forum_topic_id);", local_notify_removed_pos);
    auto null_source_log_guard_pos = messages_source.find("if(is_dialog_inited(d)&&source!=nullptr){");
    auto pending_guard_pos = poll_manager_source.find("boolhas_pending_read_poll_votes=false;");
    auto pending_call_pos = poll_manager_source.find("has_message_pending_read_poll_votes(message_full_id)");
    auto pending_block_pos = poll_manager_source.find(
        "if(!has_pending_read_poll_votes){poll->has_unread_votes_=poll_results->"
        "has_unread_votes_",
        pending_call_pos);

    ASSERT_TRUE(compare_pos != td::string::npos);
    ASSERT_TRUE(idempotence_pos != td::string::npos);
    ASSERT_TRUE(branch_pos != td::string::npos);
    ASSERT_TRUE(remove_pos != td::string::npos);
    ASSERT_TRUE(add_pos != td::string::npos);
    ASSERT_TRUE(read_all_local_signature_pos != td::string::npos);
    ASSERT_TRUE(forum_topic_reset_pos != td::string::npos);
    ASSERT_TRUE(local_counter_clear_pos != td::string::npos);
    ASSERT_TRUE(local_remove_content_pos != td::string::npos);
    ASSERT_TRUE(local_notify_removed_pos != td::string::npos);
    ASSERT_TRUE(read_all_dialog_delegate_pos != td::string::npos);
    ASSERT_TRUE(null_source_log_guard_pos != td::string::npos);
    ASSERT_TRUE(compare_pos < idempotence_pos);
    ASSERT_TRUE(idempotence_pos < branch_pos);
    ASSERT_TRUE(read_all_local_signature_pos < forum_topic_reset_pos);
    ASSERT_TRUE(local_counter_clear_pos < local_remove_content_pos);
    ASSERT_TRUE(local_remove_content_pos < local_notify_removed_pos);
    ASSERT_TRUE(local_notify_removed_pos < read_all_dialog_delegate_pos);
    ASSERT_TRUE(pending_guard_pos != td::string::npos);
    ASSERT_TRUE(pending_call_pos != td::string::npos);
    ASSERT_TRUE(pending_block_pos != td::string::npos);
    ASSERT_TRUE(pending_guard_pos < pending_call_pos);
    ASSERT_TRUE(pending_call_pos < pending_block_pos);

    checksum += static_cast<td::uint32>(messages_source.size() + poll_manager_source.size() + td_api_source.size() +
                                        static_cast<size_t>(i) + compare_pos + idempotence_pos + branch_pos +
                                        remove_pos + add_pos + read_all_local_signature_pos + forum_topic_reset_pos +
                                        local_counter_clear_pos + local_remove_content_pos + local_notify_removed_pos +
                                        read_all_dialog_delegate_pos + null_source_log_guard_pos + pending_guard_pos +
                                        pending_call_pos + pending_block_pos);
  }

  ASSERT_TRUE(checksum != 0);
}
