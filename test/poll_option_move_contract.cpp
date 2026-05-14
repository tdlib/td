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

TEST(PollOptionMoveContract, PollManagerDeclaresMessageScopedAddAndDeleteOptionEntryPoints) {
  auto poll_manager_header = read_normalized("td/telegram/PollManager.h");
  auto poll_manager_source = read_normalized("td/telegram/PollManager.cpp");

  ASSERT_TRUE(poll_manager_header.contains(
      "voidadd_poll_option(MessageFullIdmessage_full_id,td_api::object_ptr<td_api::inputPollOption>&&option,"
      "Promise<Unit>&&promise);"));
  ASSERT_TRUE(poll_manager_header.contains(
      "voiddelete_poll_option(MessageFullIdmessage_full_id,conststring&option_id,Promise<Unit>&&promise);"));
  ASSERT_TRUE(poll_manager_source.contains(
      "voidPollManager::add_poll_option(MessageFullIdmessage_full_id,td_api::object_ptr<td_api::inputPollOption>"
      "&&option,Promise<Unit>&&promise){TRY_STATUS_PROMISE(promise,td_->messages_manager_->check_can_add_poll_"
      "option(message_full_id));if(option==nullptr){"));
  ASSERT_TRUE(poll_manager_source.contains(
      "voidPollManager::delete_poll_option(MessageFullIdmessage_full_id,conststring&option_id,Promise<Unit>"
      "&&promise){autor_poll_id=td_->messages_manager_->get_message_poll_id(message_full_id,false);if(r_poll_id."
      "is_error()){returnpromise.set_error(r_poll_id.move_as_error());}td_->create_handler<DeletePollAnswerQuery>"
      "(std::move(promise))->send(message_full_id,option_id);"));
}

TEST(PollOptionMoveContract, RequestsRouteAddAndDeletePollOptionToPollManager) {
  auto requests_source = read_normalized("td/telegram/Requests.cpp");

  ASSERT_TRUE(requests_source.contains(
      "td_->poll_manager_->add_poll_option({DialogId(request.chat_id_),MessageId(request.message_id_)},std::move("
      "request.option_),std::move(promise));"));
  ASSERT_TRUE(requests_source.contains(
      "td_->poll_manager_->delete_poll_option({DialogId(request.chat_id_),MessageId(request.message_id_)},request."
      "option_id_,std::move(promise));"));
}