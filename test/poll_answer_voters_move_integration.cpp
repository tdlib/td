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

TEST(PollAnswerVotersMoveIntegration, PollManagerAnswerPathResolvesPollIdBeforeLocalVoteValidation) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/PollManager.cpp");
  auto region = normalize_for_contract(
      extract_region(source,
                     "void PollManager::set_poll_answer(MessageFullId message_full_id, vector<int32> &&option_ids, "
                     "Promise<Unit> &&promise) {",
                     "class PollManager::SetPollAnswerLogEvent {"));

  auto gate_pos = region.find(
      "TRY_RESULT_PROMISE(promise,poll_id,td_->messages_manager_->get_message_poll_id(message_full_id,"
      "false));");
  auto unique_pos = region.find("td::unique(option_ids);");
  auto local_poll_pos = region.find(
      "if(is_local_poll_id(poll_id)){returnpromise.set_error(400,\"Pollcan'tbe"
      "answered\");}");
  auto closed_pos = region.find("if(poll->is_closed_){returnpromise.set_error(400,\"Can'tanswerclosedpoll\");}");
  auto multiple_pos = region.find(
      "if(!poll->allow_multiple_answers_&&option_ids.size()>1){returnpromise.set_error("
      "400,\"Can'tchoosemorethan1optioninthepoll\");}");
  auto send_pos = region.find("do_set_poll_answer(poll_id,message_full_id,std::move(options),0,std::move(promise));");

  ASSERT_NE(td::string::npos, gate_pos);
  ASSERT_NE(td::string::npos, unique_pos);
  ASSERT_NE(td::string::npos, local_poll_pos);
  ASSERT_NE(td::string::npos, closed_pos);
  ASSERT_NE(td::string::npos, multiple_pos);
  ASSERT_NE(td::string::npos, send_pos);

  ASSERT_TRUE(gate_pos < unique_pos);
  ASSERT_TRUE(unique_pos < local_poll_pos);
  ASSERT_TRUE(local_poll_pos < closed_pos);
  ASSERT_TRUE(closed_pos < multiple_pos);
  ASSERT_TRUE(multiple_pos < send_pos);
}

TEST(PollAnswerVotersMoveIntegration, PollManagerVoterPathResolvesPollIdBeforePaginationAndVisibilityChecks) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/PollManager.cpp");
  auto region = normalize_for_contract(extract_region(
      source,
      "void PollManager::get_poll_voters(MessageFullId message_full_id, int32 option_id, int32 offset, int32 limit,",
      "void PollManager::on_get_poll_voters("));

  auto gate_pos = region.find(
      "TRY_RESULT_PROMISE(promise,poll_id,td_->messages_manager_->get_message_poll_id(message_full_id,"
      "false));");
  auto offset_pos = region.find("if(offset<0){returnpromise.set_error(400,\"Invalidoffsetspecified\");}");
  auto limit_pos = region.find("if(limit<=0){returnpromise.set_error(400,\"Parameterlimitmustbepositive\");}");
  auto poll_pos = region.find("autopoll=get_poll(poll_id);");
  auto visibility_pos = region.find(
      "if(!can_get_poll_voters(poll_id,poll)||poll->is_anonymous_){returnpromise.set_error(400,\"Pollresults"
      "can'tbereceived\");}");
  auto option_pos = region.find(
      "if(option_id<0||static_cast<size_t>(option_id)>=poll->options_.size()){return"
      "promise.set_error(400,\"InvalidoptionIDspecified\");}");

  ASSERT_NE(td::string::npos, gate_pos);
  ASSERT_NE(td::string::npos, offset_pos);
  ASSERT_NE(td::string::npos, limit_pos);
  ASSERT_NE(td::string::npos, poll_pos);
  ASSERT_NE(td::string::npos, visibility_pos);
  ASSERT_NE(td::string::npos, option_pos);

  ASSERT_TRUE(gate_pos < offset_pos);
  ASSERT_TRUE(offset_pos < limit_pos);
  ASSERT_TRUE(limit_pos < poll_pos);
  ASSERT_TRUE(poll_pos < visibility_pos);
  ASSERT_TRUE(visibility_pos < option_pos);
}

TEST(PollAnswerVotersMoveIntegration, RequestsPollAnswerAndVotersStayInsidePollManagerBoundary) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/Requests.cpp");
  auto region = normalize_for_contract(
      extract_region(source, "void Requests::on_request(uint64 id, td_api::setPollAnswer &request) {",
                     "void Requests::on_request(uint64 id, const td_api::getPollVoteStatistics &request) {"));

  ASSERT_TRUE(region.contains(
      "td_->poll_manager_->set_poll_answer({DialogId(request.chat_id_),MessageId(request.message_id_)},std::move("
      "request.option_ids_),std::move(promise));"));
  ASSERT_TRUE(region.contains(
      "td_->poll_manager_->get_poll_voters({DialogId(request.chat_id_),MessageId(request.message_id_)},request."
      "option_id_,request.offset_,request.limit_,std::move(promise));"));
  ASSERT_FALSE(region.contains("td_->messages_manager_->set_poll_answer("));
  ASSERT_FALSE(region.contains("td_->messages_manager_->get_poll_voters("));
}