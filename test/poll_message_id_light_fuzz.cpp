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

TEST(PollMessageIdLightFuzz, RandomizedProbeOrderKeepsPollIdGatePatternsPinned) {
  auto message_content_header = read_normalized("td/telegram/MessageContent.h");
  auto message_content_source = read_normalized("td/telegram/MessageContent.cpp");
  auto messages_manager_header = read_normalized("td/telegram/MessagesManager.h");
  auto messages_manager_source = read_normalized("td/telegram/MessagesManager.cpp");
  auto poll_manager_source = read_normalized("td/telegram/PollManager.cpp");

  const std::array<td::Slice, 13> required = {
      td::Slice("PollIdget_message_content_poll_id(constMessageContent*content);"),
      td::Slice("if(content==nullptr){returnPollId();}"),
      td::Slice("caseMessageContentType::Poll:returnstatic_cast<constMessagePoll*>(content)->poll_id;"),
      td::Slice("Result<PollId>get_message_poll_id(MessageFullIdmessage_full_id,boolto_stop);"),
      td::Slice("autom=get_message_force(message_full_id,\"get_message_poll_id\");"),
      td::Slice(
          "if(m->content->get_type()!=MessageContentType::Poll){returnStatus::Error(400,\"Messageisnotapoll\");}"),
      td::Slice("if(!td_->dialog_manager_->have_input_peer(message_full_id.get_dialog_id(),false,AccessRights::Read)){"
                "returnStatus::Error(400,\"Can'taccessthechat\");}"),
      td::Slice("if(m->message_id.is_scheduled()||!m->message_id.is_server()){returnStatus::Error(400,"
                "\"Wrongpollmessagespecified\");}"),
      td::Slice("if(to_stop&&!can_edit_message(message_full_id.get_dialog_id(),m,true)){returnStatus::Error(400,"
                "\"Pollcan'tbestopped\");}"),
      td::Slice("returnget_message_content_poll_id(m->content.get());"),
      td::Slice("autor_poll_id=td_->messages_manager_->get_message_poll_id(message_full_id,false);"),
      td::Slice(
          "TRY_RESULT_PROMISE(promise,poll_id,td_->messages_manager_->get_message_poll_id(message_full_id,false));"),
      td::Slice(
          "TRY_RESULT_PROMISE(promise,poll_id,td_->messages_manager_->get_message_poll_id(message_full_id,true));"),
  };

  for (int i = 0; i < 10000; i++) {
    auto idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(required.size()) - 1));
    auto snippet = required[idx];
    bool found = message_content_header.contains(snippet.str()) || message_content_source.contains(snippet.str()) ||
                 messages_manager_header.contains(snippet.str()) || messages_manager_source.contains(snippet.str()) ||
                 poll_manager_source.contains(snippet.str());
    ASSERT_TRUE(found);
  }
}

TEST(PollMessageIdLightFuzz, RandomizedProbeOrderKeepsLegacyPollIdPatternsAbsent) {
  auto message_content_source = read_normalized("td/telegram/MessageContent.cpp");
  auto poll_manager_source = read_normalized("td/telegram/PollManager.cpp");

  const std::array<td::Slice, 10> forbidden = {
      td::Slice("get_message_force(message_full_id,\"delete_poll_option\")"),
      td::Slice("get_message_force(message_full_id,\"set_poll_answer\")"),
      td::Slice("get_message_force(message_full_id,\"get_poll_voters\")"),
      td::Slice("get_message_force(message_full_id,\"stop_poll\")"),
      td::Slice("Status::Error(400,\"Messageisnotapoll\")"),
      td::Slice("Status::Error(400,\"Wrongpollmessagespecified\")"),
      td::Slice("Status::Error(400,\"Pollcan'tbestopped\")"),
      td::Slice("have_input_peer(message_full_id.get_dialog_id(),false,AccessRights::Read)"),
      td::Slice("if(content==nullptr){returnstatic_cast<constMessagePoll*>(content)->poll_id;}"),
      td::Slice("default:returnstatic_cast<constMessagePoll*>(content)->poll_id;"),
  };

  for (int i = 0; i < 10000; i++) {
    auto idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(forbidden.size()) - 1));
    auto snippet = forbidden[idx];
    bool found = message_content_source.contains(snippet.str()) || poll_manager_source.contains(snippet.str());
    ASSERT_FALSE(found);
  }
}