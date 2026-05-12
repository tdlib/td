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

TEST(PollUnreadVotesAdversarial, DuplicateTransitionsAreIgnoredToPreventCounterPoisoning) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(
      source,
      "void MessagesManager::on_update_poll_has_unread_votes(MessageFullId message_full_id, bool has_unread_votes) {",
      "void MessagesManager::on_update_message_content(MessageFullId message_full_id) {");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("constboolhas_current_unread_votes=has_unread_poll_votes(d->dialog_id,m);") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find("if(has_current_unread_votes==has_unread_votes){return;") != td::string::npos);
}

TEST(PollUnreadVotesAdversarial, RemovalHelperMustKeepPerMessageUnreadFlagAndCounterInSync) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(
      source, "bool MessagesManager::remove_message_unread_poll_votes(Dialog *d, Message *m, const char *source) {",
      "void MessagesManager::on_read_channel_inbox(ChannelId channel_id, MessageId max_message_id,");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("if(!has_unread_poll_votes(d->dialog_id,m)){returnfalse;") != td::string::npos);
  ASSERT_TRUE(normalized.find("remove_message_content_poll_has_unread_votes(td_,m->content.get());") !=
              td::string::npos);
}

TEST(PollUnreadVotesAdversarial, UpdatePathMustNotEmitDuplicateChatCounterUpdateAfterPerMessageEmission) {
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

TEST(PollUnreadVotesAdversarial,
     UpdateMessageTransitionMustNotEmitExtraChatCounterUpdateAfterPerMessageUnreadVoteUpdate) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("if(!is_scheduled&&update_message_contains_unread_poll_votes(d,old_message,new_message->"
                              "contains_unread_poll_votes,\"update_message\")){need_send_update=true;") !=
              td::string::npos);
  ASSERT_EQ(normalized.find("send_update_chat_unread_poll_vote_count(d,\"update_message\");"), td::string::npos);
}

TEST(PollUnreadVotesAdversarial, UpdatePathRejectsMissingPollContentFailClosed) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(
      source,
      "void MessagesManager::on_update_poll_has_unread_votes(MessageFullId message_full_id, bool has_unread_votes) {",
      "void MessagesManager::on_update_message_content(MessageFullId message_full_id) {");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("m->content==nullptr||m->content->get_type()!=MessageContentType::Poll") !=
              td::string::npos);
}

TEST(PollUnreadVotesAdversarial, UnreadPollVoteHelperMustNotDereferenceMissingContent) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(source,
                               "bool MessagesManager::has_unread_poll_votes(DialogId dialog_id, const Message *m) "
                               "const {",
                               "void MessagesManager::on_message_reply_info_changed(DialogId dialog_id, const Message "
                               "*m) const {");
  auto normalized = normalize_for_contract(region);

  ASSERT_EQ(normalized.find("if(is_message_forward(m)||m->content->get_type()!=MessageContentType::Poll){"
                            "returnfalse;}"),
            td::string::npos);
}

TEST(PollUnreadVotesAdversarial, UpdatePathMustNotAbortWhenDialogOrMessageLookupFails) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(
      source,
      "void MessagesManager::on_update_poll_has_unread_votes(MessageFullId message_full_id, bool has_unread_votes) {",
      "void MessagesManager::on_update_message_content(MessageFullId message_full_id) {");
  auto normalized = normalize_for_contract(region);

  ASSERT_EQ(normalized.find("CHECK(d!=nullptr);"), td::string::npos);
  ASSERT_EQ(normalized.find("CHECK(m!=nullptr);"), td::string::npos);
}

TEST(PollUnreadVotesAdversarial, PollManagerUnreadVoteNotifyPathMustNotDoubleFanoutViaOtherPollMap) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/PollManager.cpp");
  auto region = extract_region(source,
                               "void PollManager::notify_on_poll_has_unread_votes_update(PollId poll_id, bool "
                               "has_unread_votes) {",
                               "string PollManager::get_poll_database_key(PollId poll_id) {");
  auto normalized = normalize_for_contract(region);

  ASSERT_EQ(normalized.find("other_poll_messages_"), td::string::npos);
}

}  // namespace
