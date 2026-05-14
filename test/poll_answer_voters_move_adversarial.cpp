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

td::string read_normalized(td::Slice path) {
  return normalize_for_contract(td::mtproto::test::read_repo_text_file(path));
}

}  // namespace

TEST(PollAnswerVotersMoveAdversarial, MessagesManagerMustNotRetainLegacyPollAnswerOrVoterOwnership) {
  auto messages_manager_header = read_normalized("td/telegram/MessagesManager.h");
  auto messages_manager_source = read_normalized("td/telegram/MessagesManager.cpp");

  ASSERT_FALSE(messages_manager_header.contains(
      "voidset_poll_answer(MessageFullIdmessage_full_id,vector<int32>&&option_ids,Promise<Unit>&&promise);"));
  ASSERT_FALSE(messages_manager_header.contains(
      "voidget_poll_voters(MessageFullIdmessage_full_id,int32option_id,int32offset,int32limit,Promise<td_api::"
      "object_ptr<td_api::pollVoters>>&&promise);"));
  ASSERT_FALSE(messages_manager_source.contains(
      "voidMessagesManager::set_poll_answer(MessageFullIdmessage_full_id,vector<int32>&&option_ids,Promise<Unit>"
      "&&promise){"));
  ASSERT_FALSE(messages_manager_source.contains(
      "voidMessagesManager::get_poll_voters(MessageFullIdmessage_full_id,int32option_id,int32offset,int32limit,"
      "Promise<td_api::object_ptr<td_api::pollVoters>>&&promise){"));
  ASSERT_FALSE(messages_manager_source.contains(
      "set_message_content_poll_answer(td_,m->content.get(),message_full_id,std::move(option_ids),std::move("
      "promise));"));
  ASSERT_FALSE(messages_manager_source.contains(
      "get_message_content_poll_voters(td_,m->content.get(),message_full_id,option_id,offset,limit,std::move("
      "promise));"));
}

TEST(PollAnswerVotersMoveAdversarial, MessageContentMustNotExposeLegacyAnswerOrVoterHelpers) {
  auto message_content_header = read_normalized("td/telegram/MessageContent.h");
  auto message_content_source = read_normalized("td/telegram/MessageContent.cpp");

  ASSERT_FALSE(message_content_header.contains(
      "voidset_message_content_poll_answer(Td*td,constMessageContent*content,MessageFullIdmessage_full_id,vector<"
      "int32>&&option_ids,Promise<Unit>&&promise);"));
  ASSERT_FALSE(message_content_header.contains(
      "voidget_message_content_poll_voters(Td*td,constMessageContent*content,MessageFullIdmessage_full_id,int32"
      "option_id,int32offset,int32limit,Promise<td_api::object_ptr<td_api::pollVoters>>&&promise);"));
  ASSERT_FALSE(message_content_source.contains(
      "voidset_message_content_poll_answer(Td*td,constMessageContent*content,MessageFullIdmessage_full_id,vector<"
      "int32>&&option_ids,Promise<Unit>&&promise){"));
  ASSERT_FALSE(message_content_source.contains(
      "voidget_message_content_poll_voters(Td*td,constMessageContent*content,MessageFullIdmessage_full_id,int32"
      "option_id,int32offset,int32limit,Promise<td_api::object_ptr<td_api::pollVoters>>&&promise){"));
}

TEST(PollAnswerVotersMoveAdversarial, RequestsAndPollManagerMustNotReintroduceLegacyRoutingOrSignatures) {
  auto requests_source = read_normalized("td/telegram/Requests.cpp");
  auto poll_manager_header = read_normalized("td/telegram/PollManager.h");
  auto poll_manager_source = read_normalized("td/telegram/PollManager.cpp");

  ASSERT_FALSE(requests_source.contains(
      "td_->messages_manager_->set_poll_answer({DialogId(request.chat_id_),MessageId(request.message_id_)},"));
  ASSERT_FALSE(requests_source.contains(
      "td_->messages_manager_->get_poll_voters({DialogId(request.chat_id_),MessageId(request.message_id_)},"));
  ASSERT_FALSE(poll_manager_header.contains(
      "voidset_poll_answer(PollIdpoll_id,MessageFullIdmessage_full_id,vector<int32>&&option_ids,Promise<Unit>&&"
      "promise);"));
  ASSERT_FALSE(poll_manager_header.contains(
      "voidget_poll_voters(PollIdpoll_id,MessageFullIdmessage_full_id,int32option_id,int32offset,int32limit,"
      "Promise<td_api::object_ptr<td_api::pollVoters>>&&promise);"));
  ASSERT_FALSE(poll_manager_source.contains(
      "voidPollManager::set_poll_answer(PollIdpoll_id,MessageFullIdmessage_full_id,vector<int32>&&option_ids,"
      "Promise<Unit>&&promise){"));
  ASSERT_FALSE(poll_manager_source.contains(
      "voidPollManager::get_poll_voters(PollIdpoll_id,MessageFullIdmessage_full_id,int32option_id,int32offset,"
      "int32limit,Promise<td_api::object_ptr<td_api::pollVoters>>&&promise){"));
}