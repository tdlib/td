// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <array>

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

TEST(PollAnswerVotersMoveLightFuzz, RandomizedProbeOrderKeepsMovedPollEntryPointsPinned) {
  auto poll_manager_header = read_normalized("td/telegram/PollManager.h");
  auto poll_manager_source = read_normalized("td/telegram/PollManager.cpp");
  auto requests_source = read_normalized("td/telegram/Requests.cpp");

  const std::array<td::Slice, 8> required = {
      td::Slice("voidset_poll_answer(MessageFullIdmessage_full_id,vector<int32>&&option_ids,Promise<Unit>&&"
                "promise);"),
      td::Slice("voidget_poll_voters(MessageFullIdmessage_full_id,int32option_id,int32offset,int32limit,Promise<"
                "td_api::object_ptr<td_api::pollVoters>>&&promise);"),
      td::Slice("voidPollManager::set_poll_answer(MessageFullIdmessage_full_id,vector<int32>&&option_ids,Promise<"
                "Unit>&&promise){TRY_RESULT_PROMISE(promise,poll_id,td_->messages_manager_->get_message_poll_id("
                "message_full_id,false));td::unique(option_ids);"),
      td::Slice("if(is_local_poll_id(poll_id)){returnpromise.set_error(400,\"Pollcan'tbeanswered\");}"),
      td::Slice("do_set_poll_answer(poll_id,message_full_id,std::move(options),0,std::move(promise));"),
      td::Slice("voidPollManager::get_poll_voters(MessageFullIdmessage_full_id,int32option_id,int32offset,int32"
                "limit,Promise<td_api::object_ptr<td_api::pollVoters>>&&promise){TRY_RESULT_PROMISE(promise,poll_"
                "id,td_->messages_manager_->get_message_poll_id(message_full_id,false));if(offset<0){"),
      td::Slice("td_->poll_manager_->set_poll_answer({DialogId(request.chat_id_),MessageId(request.message_id_)},"
                "std::move(request.option_ids_),std::move(promise));"),
      td::Slice("td_->poll_manager_->get_poll_voters({DialogId(request.chat_id_),MessageId(request.message_id_)},"
                "request.option_id_,request.offset_,request.limit_,std::move(promise));"),
  };

  for (int i = 0; i < 10000; i++) {
    auto idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(required.size()) - 1));
    auto snippet = required[idx];
    bool found = poll_manager_header.contains(snippet.str()) || poll_manager_source.contains(snippet.str()) ||
                 requests_source.contains(snippet.str());
    ASSERT_TRUE(found);
  }
}

TEST(PollAnswerVotersMoveLightFuzz, RandomizedProbeOrderKeepsLegacyPollRoutingPatternsAbsent) {
  auto message_content_header = read_normalized("td/telegram/MessageContent.h");
  auto message_content_source = read_normalized("td/telegram/MessageContent.cpp");
  auto messages_manager_header = read_normalized("td/telegram/MessagesManager.h");
  auto messages_manager_source = read_normalized("td/telegram/MessagesManager.cpp");
  auto poll_manager_header = read_normalized("td/telegram/PollManager.h");
  auto poll_manager_source = read_normalized("td/telegram/PollManager.cpp");
  auto requests_source = read_normalized("td/telegram/Requests.cpp");

  const std::array<td::Slice, 12> forbidden = {
      td::Slice("voidset_message_content_poll_answer(Td*td,constMessageContent*content,MessageFullIdmessage_full_"
                "id,vector<int32>&&option_ids,Promise<Unit>&&promise);"),
      td::Slice("voidget_message_content_poll_voters(Td*td,constMessageContent*content,MessageFullIdmessage_full_"
                "id,int32option_id,int32offset,int32limit,Promise<td_api::object_ptr<td_api::pollVoters>>&&"
                "promise);"),
      td::Slice("voidMessagesManager::set_poll_answer(MessageFullIdmessage_full_id,vector<int32>&&option_ids,"
                "Promise<Unit>&&promise){"),
      td::Slice("voidMessagesManager::get_poll_voters(MessageFullIdmessage_full_id,int32option_id,int32offset,"
                "int32limit,Promise<td_api::object_ptr<td_api::pollVoters>>&&promise){"),
      td::Slice("td_->messages_manager_->set_poll_answer({DialogId(request.chat_id_),MessageId(request.message_"
                "id)},"),
      td::Slice("td_->messages_manager_->get_poll_voters({DialogId(request.chat_id_),MessageId(request.message_"
                "id)},"),
      td::Slice("voidset_poll_answer(PollIdpoll_id,MessageFullIdmessage_full_id,vector<int32>&&option_ids,"
                "Promise<Unit>&&promise);"),
      td::Slice("voidget_poll_voters(PollIdpoll_id,MessageFullIdmessage_full_id,int32option_id,int32offset,int32"
                "limit,Promise<td_api::object_ptr<td_api::pollVoters>>&&promise);"),
      td::Slice("voidPollManager::set_poll_answer(PollIdpoll_id,MessageFullIdmessage_full_id,vector<int32>&&"
                "option_ids,Promise<Unit>&&promise){"),
      td::Slice("voidPollManager::get_poll_voters(PollIdpoll_id,MessageFullIdmessage_full_id,int32option_id,int32"
                "offset,int32limit,Promise<td_api::object_ptr<td_api::pollVoters>>&&promise){"),
      td::Slice("set_message_content_poll_answer(td_,m->content.get(),message_full_id,std::move(option_ids),std::"
                "move(promise));"),
      td::Slice("get_message_content_poll_voters(td_,m->content.get(),message_full_id,option_id,offset,limit,std::"
                "move(promise));"),
  };

  for (int i = 0; i < 10000; i++) {
    auto idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(forbidden.size()) - 1));
    auto snippet = forbidden[idx];
    bool found = message_content_header.contains(snippet.str()) || message_content_source.contains(snippet.str()) ||
                 messages_manager_header.contains(snippet.str()) || messages_manager_source.contains(snippet.str()) ||
                 poll_manager_header.contains(snippet.str()) || poll_manager_source.contains(snippet.str()) ||
                 requests_source.contains(snippet.str());
    ASSERT_FALSE(found);
  }
}