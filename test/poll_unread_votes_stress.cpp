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
    auto td_api_source = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/generate/scheme/td_api.tl"));

    ASSERT_NE(td::string::npos, messages_source.find("returnm->contains_unread_poll_votes;"));
    ASSERT_NE(td::string::npos,
              messages_source.find("constboolhas_current_unread_votes=has_unread_poll_votes(d->dialog_id,m);"));
    ASSERT_NE(td::string::npos, messages_source.find("if(has_current_unread_votes==has_unread_votes){return;}"));
    ASSERT_NE(td::string::npos,
              messages_source.find("remove_message_unread_poll_votes(d,m,\"on_update_poll_has_unread_votes\");"));
    ASSERT_NE(td::string::npos, messages_source.find("m->contains_unread_poll_votes=true;"));
    ASSERT_NE(td::string::npos, messages_source.find("m->contains_unread_poll_votes=false;"));
    ASSERT_NE(td::string::npos,
              messages_source.find("remove_message_content_poll_has_unread_votes(td_,m->content.get());"));
    ASSERT_NE(td::string::npos,
              messages_source.find(
                  "send_update_message_contains_unread_poll_votes(d->dialog_id,m,d->unread_poll_vote_count);"));
    ASSERT_NE(td::string::npos,
              messages_source.find("autois_update_sent=read_all_local_dialog_poll_votes(dialog_id,forum_topic_id);"));
    ASSERT_NE(td::string::npos,
              messages_source.find(
                  "if(!is_update_sent){send_update_chat_unread_poll_vote_count(d,\"read_all_dialog_poll_votes\");}"));
    ASSERT_NE(td::string::npos, messages_source.find("on_dialog_updated(dialog_id,\"read_all_dialog_poll_votes\");"));

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
    ASSERT_EQ(td::string::npos, td_api_source.find("updateMessageUnreadPollVotes"));

    auto compare_pos = messages_source.find("constboolhas_current_unread_votes=has_unread_poll_votes(d->dialog_id,m);");
    auto idempotence_pos = messages_source.find("if(has_current_unread_votes==has_unread_votes){return;}");
    auto branch_pos = messages_source.find("if(!has_unread_votes){", idempotence_pos);
    auto remove_pos =
        messages_source.find("remove_message_unread_poll_votes(d,m,\"on_update_poll_has_unread_votes\");", branch_pos);
    auto add_pos = messages_source.find("m->contains_unread_poll_votes=true;", branch_pos);
    auto read_all_local_pos =
        messages_source.find("autois_update_sent=read_all_local_dialog_poll_votes(dialog_id,forum_topic_id);");
    auto conditional_poll_update_pos = messages_source.find("if(!is_update_sent){", read_all_local_pos);
    auto read_all_chat_update_pos = messages_source.find(
        "send_update_chat_unread_poll_vote_count(d,\"read_all_dialog_poll_votes\");", conditional_poll_update_pos);
    auto read_all_dialog_updated_pos = messages_source.find(
        "on_dialog_updated(dialog_id,\"read_all_dialog_poll_votes\");", conditional_poll_update_pos);

    ASSERT_TRUE(compare_pos != td::string::npos);
    ASSERT_TRUE(idempotence_pos != td::string::npos);
    ASSERT_TRUE(branch_pos != td::string::npos);
    ASSERT_TRUE(remove_pos != td::string::npos);
    ASSERT_TRUE(add_pos != td::string::npos);
    ASSERT_TRUE(read_all_local_pos != td::string::npos);
    ASSERT_TRUE(conditional_poll_update_pos != td::string::npos);
    ASSERT_TRUE(read_all_chat_update_pos != td::string::npos);
    ASSERT_TRUE(read_all_dialog_updated_pos != td::string::npos);
    ASSERT_TRUE(compare_pos < idempotence_pos);
    ASSERT_TRUE(idempotence_pos < branch_pos);
    ASSERT_TRUE(read_all_local_pos < conditional_poll_update_pos);
    ASSERT_TRUE(conditional_poll_update_pos < read_all_chat_update_pos);
    ASSERT_TRUE(conditional_poll_update_pos < read_all_dialog_updated_pos);

    checksum +=
        static_cast<td::uint32>(messages_source.size() + td_api_source.size() + static_cast<size_t>(i) + compare_pos +
                                idempotence_pos + branch_pos + remove_pos + add_pos + read_all_local_pos +
                                conditional_poll_update_pos + read_all_chat_update_pos + read_all_dialog_updated_pos);
  }

  ASSERT_TRUE(checksum != 0);
}
