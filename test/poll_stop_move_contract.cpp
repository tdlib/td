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

TEST(PollStopMoveContract, PollManagerDeclaresMessageScopedStopPollEntryPoint) {
  auto poll_manager_header = read_normalized("td/telegram/PollManager.h");
  auto poll_manager_source = read_normalized("td/telegram/PollManager.cpp");

  ASSERT_TRUE(poll_manager_header.contains(
      "voidstop_poll(MessageFullIdmessage_full_id,td_api::object_ptr<td_api::ReplyMarkup>&&reply_markup,Promise<"
      "Unit>&&promise);"));
  ASSERT_TRUE(poll_manager_source.contains(
      "voidPollManager::stop_poll(MessageFullIdmessage_full_id,td_api::object_ptr<td_api::ReplyMarkup>&&reply_"
      "markup,Promise<Unit>&&promise){TRY_RESULT_PROMISE(promise,poll_id,td_->messages_manager_->get_message_"
      "poll_id(message_full_id,true));if(get_poll_is_closed(poll_id)){"));
  ASSERT_TRUE(poll_manager_source.contains(
      "do_stop_poll(poll_id,message_full_id,std::move(new_reply_markup),0,std::move(promise));"));
}

TEST(PollStopMoveContract, RequestsRouteStopPollToPollManager) {
  auto requests_source = read_normalized("td/telegram/Requests.cpp");

  ASSERT_TRUE(requests_source.contains(
      "td_->poll_manager_->stop_poll({DialogId(request.chat_id_),MessageId(request.message_id_)},std::move("
      "request.reply_markup_),std::move(promise));"));
}