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

TEST(PollAnswerVotersMoveContract, PollManagerDeclaresMessageScopedAnswerAndVoterEntryPoints) {
  auto poll_manager_header = read_normalized("td/telegram/PollManager.h");
  auto poll_manager_source = read_normalized("td/telegram/PollManager.cpp");

  ASSERT_TRUE(poll_manager_header.contains(
      "voidset_poll_answer(MessageFullIdmessage_full_id,vector<int32>&&option_ids,Promise<Unit>&&promise);"));
  ASSERT_TRUE(poll_manager_header.contains(
      "voidget_poll_voters(MessageFullIdmessage_full_id,int32option_id,int32offset,int32limit,Promise<td_api::"
      "object_ptr<td_api::pollVoters>>&&promise);"));
  ASSERT_TRUE(poll_manager_source.contains(
      "voidPollManager::set_poll_answer(MessageFullIdmessage_full_id,vector<int32>&&option_ids,Promise<Unit>&&"
      "promise){TRY_RESULT_PROMISE(promise,poll_id,td_->messages_manager_->get_message_poll_id(message_full_id,"
      "false));td::unique(option_ids);"));
  ASSERT_TRUE(poll_manager_source.contains(
      "voidPollManager::get_poll_voters(MessageFullIdmessage_full_id,int32option_id,int32offset,int32limit,"
      "Promise<td_api::object_ptr<td_api::pollVoters>>&&promise){TRY_RESULT_PROMISE(promise,poll_id,td_->"
      "messages_manager_->get_message_poll_id(message_full_id,false));if(offset<0){"));
}

TEST(PollAnswerVotersMoveContract, RequestsRoutePollAnswerAndVoterQueriesToPollManager) {
  auto requests_source = read_normalized("td/telegram/Requests.cpp");

  ASSERT_TRUE(requests_source.contains(
      "td_->poll_manager_->set_poll_answer({DialogId(request.chat_id_),MessageId(request.message_id_)},std::move("
      "request.option_ids_),std::move(promise));"));
  ASSERT_TRUE(requests_source.contains(
      "td_->poll_manager_->get_poll_voters({DialogId(request.chat_id_),MessageId(request.message_id_)},request."
      "option_id_,request.offset_,request.limit_,std::move(promise));"));
}