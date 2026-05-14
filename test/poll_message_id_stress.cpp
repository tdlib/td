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

TEST(PollMessageIdStress, RepeatedSourceReadsKeepPollIdGateInvariantsStable) {
  constexpr int kIterations = 3000;
  td::uint32 checksum = 0;

  for (int i = 0; i < kIterations; i++) {
    auto message_content_header = read_normalized("td/telegram/MessageContent.h");
    auto message_content_source = read_normalized("td/telegram/MessageContent.cpp");
    auto messages_manager_header = read_normalized("td/telegram/MessagesManager.h");
    auto messages_manager_source = read_normalized("td/telegram/MessagesManager.cpp");
    auto poll_manager_source = read_normalized("td/telegram/PollManager.cpp");

    ASSERT_TRUE(message_content_header.contains("PollIdget_message_content_poll_id(constMessageContent*content);"));
    ASSERT_TRUE(message_content_source.contains("if(content==nullptr){returnPollId();}"));
    ASSERT_TRUE(message_content_source.contains(
        "caseMessageContentType::Poll:returnstatic_cast<constMessagePoll*>(content)->poll_id;"));
    ASSERT_TRUE(messages_manager_header.contains(
        "Result<PollId>get_message_poll_id(MessageFullIdmessage_full_id,boolto_stop);"));
    ASSERT_TRUE(messages_manager_source.contains("autom=get_message_force(message_full_id,\"get_message_poll_id\");"));
    ASSERT_TRUE(messages_manager_source.contains(
        "if(m->content->get_type()!=MessageContentType::Poll){returnStatus::Error(400,\"Messageisnotapoll\");}"));
    ASSERT_TRUE(messages_manager_source.contains(
        "if(!td_->dialog_manager_->have_input_peer(message_full_id.get_dialog_id(),false,AccessRights::Read)){"
        "returnStatus::Error(400,\"Can'taccessthechat\");}"));
    ASSERT_TRUE(
        messages_manager_source.contains("if(m->message_id.is_scheduled()||!m->message_id.is_server()){returnStatus::"
                                         "Error(400,\"Wrongpollmessagespecified\");}"));
    ASSERT_TRUE(
        messages_manager_source.contains("if(to_stop&&!can_edit_message(message_full_id.get_dialog_id(),m,true)){"
                                         "returnStatus::Error(400,\"Pollcan'tbestopped\");}"));
    ASSERT_TRUE(messages_manager_source.contains("returnget_message_content_poll_id(m->content.get());"));
    ASSERT_TRUE(poll_manager_source.contains(
        "autor_poll_id=td_->messages_manager_->get_message_poll_id(message_full_id,false);"));
    ASSERT_TRUE(poll_manager_source.contains(
        "TRY_RESULT_PROMISE(promise,poll_id,td_->messages_manager_->get_message_poll_id(message_full_id,false));"));
    ASSERT_TRUE(poll_manager_source.contains(
        "TRY_RESULT_PROMISE(promise,poll_id,td_->messages_manager_->get_message_poll_id(message_full_id,true));"));

    ASSERT_FALSE(poll_manager_source.contains("get_message_force(message_full_id,\"delete_poll_option\")"));
    ASSERT_FALSE(poll_manager_source.contains("get_message_force(message_full_id,\"set_poll_answer\")"));
    ASSERT_FALSE(poll_manager_source.contains("get_message_force(message_full_id,\"get_poll_voters\")"));
    ASSERT_FALSE(poll_manager_source.contains("get_message_force(message_full_id,\"stop_poll\")"));
    ASSERT_FALSE(poll_manager_source.contains("Status::Error(400,\"Messageisnotapoll\")"));
    ASSERT_FALSE(poll_manager_source.contains("Status::Error(400,\"Wrongpollmessagespecified\")"));
    ASSERT_FALSE(poll_manager_source.contains("Status::Error(400,\"Pollcan'tbestopped\")"));
    ASSERT_FALSE(
        poll_manager_source.contains("have_input_peer(message_full_id.get_dialog_id(),false,AccessRights::Read)"));
    ASSERT_FALSE(message_content_source.contains(
        "if(content==nullptr){returnstatic_cast<constMessagePoll*>(content)->poll_id;}"));
    ASSERT_FALSE(message_content_source.contains("default:returnstatic_cast<constMessagePoll*>(content)->poll_id;"));

    checksum += static_cast<td::uint32>(message_content_header.size() + message_content_source.size() +
                                        messages_manager_header.size() + messages_manager_source.size() +
                                        poll_manager_source.size() + static_cast<size_t>(i));
  }

  ASSERT_TRUE(checksum != 0);
}