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

TEST(PollStopMoveIntegration, PollManagerStopPathResolvesPollIdBeforeCloseAndReplyMarkupChecks) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/PollManager.cpp");
  auto region = normalize_for_contract(
      extract_region(source,
                     "void PollManager::stop_poll(MessageFullId message_full_id, td_api::object_ptr<td_api::"
                     "ReplyMarkup> &&reply_markup,",
                     "class PollManager::StopPollLogEvent {"));

  auto gate_pos = region.find(
      "TRY_RESULT_PROMISE(promise,poll_id,td_->messages_manager_->get_message_poll_id(message_full_id,true));");
  auto already_closed_pos =
      region.find("if(get_poll_is_closed(poll_id)){returnpromise.set_error(400,\"Pollhasalreadybeenclosed\");}");
  auto reply_markup_pos = region.find(
      "TRY_RESULT_PROMISE(promise,new_reply_markup,get_inline_reply_markup(std::move(reply_markup),td_->"
      "auth_manager_->is_bot(),td_->messages_manager_->has_message_sender_user_id(message_full_id)));");
  auto local_poll_pos = region.find(
      "if(is_local_poll_id(poll_id)){LOG(ERROR)<<\"Receivelocal\"<<poll_id<<\"from\"<<message_full_id<<"
      "\"instop_poll\";stop_local_poll(poll_id);promise.set_value(Unit());return;}");
  auto editable_pos = region.find("autopoll=get_poll_editable(poll_id);CHECK(poll!=nullptr);");
  auto cached_closed_pos = region.find("if(poll->is_closed_){promise.set_value(Unit());return;}");
  auto generation_pos = region.find("++current_generation_;");
  auto save_pos = region.find("save_poll(poll,poll_id);");
  auto notify_pos = region.find("notify_on_poll_update(poll_id);");
  auto send_pos =
      region.find("do_stop_poll(poll_id,message_full_id,std::move(new_reply_markup),0,std::move(promise));");

  ASSERT_NE(td::string::npos, gate_pos);
  ASSERT_NE(td::string::npos, already_closed_pos);
  ASSERT_NE(td::string::npos, reply_markup_pos);
  ASSERT_NE(td::string::npos, local_poll_pos);
  ASSERT_NE(td::string::npos, editable_pos);
  ASSERT_NE(td::string::npos, cached_closed_pos);
  ASSERT_NE(td::string::npos, generation_pos);
  ASSERT_NE(td::string::npos, save_pos);
  ASSERT_NE(td::string::npos, notify_pos);
  ASSERT_NE(td::string::npos, send_pos);

  ASSERT_TRUE(gate_pos < already_closed_pos);
  ASSERT_TRUE(already_closed_pos < reply_markup_pos);
  ASSERT_TRUE(reply_markup_pos < local_poll_pos);
  ASSERT_TRUE(local_poll_pos < editable_pos);
  ASSERT_TRUE(editable_pos < cached_closed_pos);
  ASSERT_TRUE(cached_closed_pos < generation_pos);
  ASSERT_TRUE(generation_pos < save_pos);
  ASSERT_TRUE(save_pos < notify_pos);
  ASSERT_TRUE(notify_pos < send_pos);
}

TEST(PollStopMoveIntegration, RequestsStopPollStaysInsidePollManagerBoundary) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/Requests.cpp");
  auto region = normalize_for_contract(
      extract_region(source, "void Requests::on_request(uint64 id, td_api::stopPoll &request) {",
                     "void Requests::on_request(uint64 id, td_api::addChecklistTasks &request) {"));

  ASSERT_TRUE(region.contains(
      "td_->poll_manager_->stop_poll({DialogId(request.chat_id_),MessageId(request.message_id_)},std::move("
      "request.reply_markup_),std::move(promise));"));
  ASSERT_FALSE(region.contains("td_->messages_manager_->stop_poll("));
}