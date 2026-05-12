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

TEST(PollUnreadVotesContract, ReadAllPathUsesConditionalChatLevelFanoutToAvoidDuplicateEmission) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(
      source, "void MessagesManager::read_all_dialog_poll_votes(DialogId dialog_id, ForumTopicId forum_topic_id,",
      "void MessagesManager::read_message_content_from_updates(MessageId message_id, int32 read_date) {");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("autois_update_sent=read_all_local_dialog_poll_votes(dialog_id,forum_topic_id);") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find("if(!is_update_sent){") != td::string::npos);
  ASSERT_TRUE(normalized.find("send_update_chat_unread_poll_vote_count(d,\"read_all_dialog_poll_votes\");") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find("on_dialog_updated(dialog_id,\"read_all_dialog_poll_votes\");") != td::string::npos);
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

TEST(PollUnreadVotesContract, TdApiSchemaPinsUnreadPollVoteMessageStateAndUpdateContract) {
  auto source = td::mtproto::test::read_repo_text_file("td/generate/scheme/td_api.tl");

  ASSERT_TRUE(source.find("contains_unread_poll_votes:Bool") != td::string::npos);
  ASSERT_TRUE(source.find("updateMessageContainsUnreadPollVotes") != td::string::npos);
  ASSERT_TRUE(source.find("contains_unread_poll_votes:Bool unread_poll_vote_count:int32 = Update;") !=
              td::string::npos);
}

}  // namespace
