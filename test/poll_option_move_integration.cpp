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

}  // namespace

TEST(PollOptionMoveIntegration, PollManagerAddPathChecksSharedEligibilityBeforeNullAndLengthValidation) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/PollManager.cpp");
  auto region = normalize_for_contract(
      extract_region(source,
                     "void PollManager::add_poll_option(MessageFullId message_full_id, td_api::object_ptr<td_api::"
                     "inputPollOption> &&option,",
                     "void PollManager::delete_poll_option("));

  auto gate_pos =
      region.find("TRY_STATUS_PROMISE(promise,td_->messages_manager_->check_can_add_poll_option(message_full_id));");
  auto null_pos = region.find("if(option==nullptr){returnpromise.set_error(400,\"Polloptionmustbenon-empty\");}");
  auto format_pos = region.find(
      "TRY_RESULT_PROMISE(promise,poll_option,get_formatted_text(td_,message_full_id.get_dialog_id(),std::move("
      "option->text_),td_->auth_manager_->is_bot(),false,true,false));");
  auto limit_const_pos = region.find("constexprsize_tMAX_POLL_OPTION_LENGTH=100;");
  auto limit_pos = region.find(
      "if(utf8_length(poll_option.text)>MAX_POLL_OPTION_LENGTH){returnpromise.set_error(400,PSLICE()<<\"Poll"
      "optionslengthmustnotexceed\"<<MAX_POLL_OPTION_LENGTH);}");
  auto send_pos =
      region.find("td_->create_handler<AddPollAnswerQuery>(std::move(promise))->send(message_full_id,poll_option);");

  ASSERT_NE(td::string::npos, gate_pos);
  ASSERT_NE(td::string::npos, null_pos);
  ASSERT_NE(td::string::npos, format_pos);
  ASSERT_NE(td::string::npos, limit_const_pos);
  ASSERT_NE(td::string::npos, limit_pos);
  ASSERT_NE(td::string::npos, send_pos);

  ASSERT_TRUE(gate_pos < null_pos);
  ASSERT_TRUE(null_pos < format_pos);
  ASSERT_TRUE(format_pos < limit_const_pos);
  ASSERT_TRUE(limit_const_pos < limit_pos);
  ASSERT_TRUE(limit_pos < send_pos);
}

TEST(PollOptionMoveIntegration, PollManagerDeletePathResolvesPollIdBeforeDeleteQueryDispatch) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/PollManager.cpp");
  auto region = normalize_for_contract(
      extract_region(source,
                     "void PollManager::delete_poll_option(MessageFullId message_full_id, const string &option_id, "
                     "Promise<Unit> &&promise) {",
                     "void PollManager::set_poll_answer("));

  auto gate_pos = region.find("autor_poll_id=td_->messages_manager_->get_message_poll_id(message_full_id,false);");
  auto error_pos = region.find("if(r_poll_id.is_error()){returnpromise.set_error(r_poll_id.move_as_error());}");
  auto send_pos =
      region.find("td_->create_handler<DeletePollAnswerQuery>(std::move(promise))->send(message_full_id,option_id);");

  ASSERT_NE(td::string::npos, gate_pos);
  ASSERT_NE(td::string::npos, error_pos);
  ASSERT_NE(td::string::npos, send_pos);

  ASSERT_TRUE(gate_pos < error_pos);
  ASSERT_TRUE(error_pos < send_pos);
}

TEST(PollOptionMoveIntegration, RequestsAddAndDeletePollOptionStayInsidePollManagerBoundary) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/Requests.cpp");
  auto region = normalize_for_contract(
      extract_region(source, "void Requests::on_request(uint64 id, td_api::addPollOption &request) {",
                     "void Requests::on_request(uint64 id, td_api::setPollAnswer &request) {"));

  ASSERT_TRUE(region.contains(
      "td_->poll_manager_->add_poll_option({DialogId(request.chat_id_),MessageId(request.message_id_)},std::move("
      "request.option_),std::move(promise));"));
  ASSERT_TRUE(region.contains(
      "td_->poll_manager_->delete_poll_option({DialogId(request.chat_id_),MessageId(request.message_id_)},request."
      "option_id_,std::move(promise));"));
  ASSERT_FALSE(region.contains("td_->messages_manager_->add_poll_option("));
  ASSERT_FALSE(region.contains("td_->messages_manager_->delete_poll_option("));
}