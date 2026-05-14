// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
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

td::string read_normalized(td::Slice path) {
  return normalize_for_contract(td::mtproto::test::read_repo_text_file(path));
}

}  // namespace

TEST(PollMessageIdIntegration, MessagesManagerPollIdGateValidatesBeforeReturningContentPollId) {
  auto messages_manager_source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto gate_region = normalize_for_contract(extract_region(
      messages_manager_source,
      "Result<PollId> MessagesManager::get_message_poll_id(MessageFullId message_full_id, bool to_stop) {",
      "Result<ServerMessageId> MessagesManager::get_group_call_message_id("));

  auto get_message_pos = gate_region.find("autom=get_message_force(message_full_id,\"get_message_poll_id\");");
  auto not_poll_pos = gate_region.find(
      "if(m->content->get_type()!=MessageContentType::Poll){returnStatus::Error(400,\"Messageisnotapoll\");}");
  auto access_pos = gate_region.find(
      "if(!td_->dialog_manager_->have_input_peer(message_full_id.get_dialog_id(),false,"
      "AccessRights::Read)){returnStatus::Error(400,\"Can'taccessthechat\");}");
  auto wrong_message_pos = gate_region.find(
      "if(m->message_id.is_scheduled()||!m->message_id.is_server()){returnStatus::Error(400,\"Wrongpollmessage"
      "specified\");}");
  auto stop_permission_pos = gate_region.find(
      "if(to_stop&&!can_edit_message(message_full_id.get_dialog_id(),m,true)){returnStatus::Error(400,\"Pollcan't"
      "bestopped\");}");
  auto return_poll_id_pos = gate_region.find("returnget_message_content_poll_id(m->content.get());");

  ASSERT_NE(td::string::npos, get_message_pos);
  ASSERT_NE(td::string::npos, not_poll_pos);
  ASSERT_NE(td::string::npos, access_pos);
  ASSERT_NE(td::string::npos, wrong_message_pos);
  ASSERT_NE(td::string::npos, stop_permission_pos);
  ASSERT_NE(td::string::npos, return_poll_id_pos);

  ASSERT_TRUE(get_message_pos < not_poll_pos);
  ASSERT_TRUE(not_poll_pos < access_pos);
  ASSERT_TRUE(access_pos < wrong_message_pos);
  ASSERT_TRUE(wrong_message_pos < stop_permission_pos);
  ASSERT_TRUE(stop_permission_pos < return_poll_id_pos);
}

TEST(PollMessageIdIntegration, PollOperationsDelegateToCentralPollIdGateBeforeLocalChecks) {
  auto poll_manager_source = read_normalized("td/telegram/PollManager.cpp");

  ASSERT_TRUE(poll_manager_source.contains(
      "autor_poll_id=td_->messages_manager_->get_message_poll_id(message_full_id,false);if(r_poll_id.is_error()){"
      "returnpromise.set_error(r_poll_id.move_as_error());}td_->create_handler<DeletePollAnswerQuery>(std::move("
      "promise))->send(message_full_id,option_id);"));
  ASSERT_TRUE(poll_manager_source.contains(
      "TRY_RESULT_PROMISE(promise,poll_id,td_->messages_manager_->get_message_poll_id(message_full_id,false));td::"
      "unique(option_ids);"));
  ASSERT_TRUE(poll_manager_source.contains(
      "TRY_RESULT_PROMISE(promise,poll_id,td_->messages_manager_->get_message_poll_id(message_full_id,false));if("
      "offset<0){"));
  ASSERT_TRUE(poll_manager_source.contains(
      "TRY_RESULT_PROMISE(promise,poll_id,td_->messages_manager_->get_message_poll_id(message_full_id,true));if("
      "get_poll_is_closed(poll_id)){"));
}