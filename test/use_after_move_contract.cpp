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
    auto byte = static_cast<unsigned char>(c);
    if (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

td::string extract_source_region(td::Slice source, td::Slice begin_marker, td::Slice end_marker) {
  auto source_text = source.str();
  auto begin = source_text.find(begin_marker.str());
  CHECK(begin != td::string::npos);
  auto end = source_text.find(end_marker.str(), begin);
  CHECK(end != td::string::npos);
  CHECK(begin < end);
  return source_text.substr(begin, end - begin);
}

TEST(UseAfterMoveContract, CommonDialogManagerComputesTotalCountBeforeMovingChatsVector) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/CommonDialogManager.cpp");
  auto region = extract_source_region(source, "case telegram_api::messages_chats::ID: {",
                                      "case telegram_api::messages_chatsSlice::ID: {");
  auto normalized = normalize_for_contract(region);

  auto total_count_pos = normalized.find("autototal_count=narrow_cast<int32>(chats->chats_.size());");
  auto move_pos = normalized.find("std::move(chats->chats_)");

  ASSERT_TRUE(total_count_pos != td::string::npos);
  ASSERT_TRUE(move_pos != td::string::npos);
  ASSERT_TRUE(total_count_pos < move_pos);
  ASSERT_TRUE(normalized.find("std::move(chats->chats_),total_count);") != td::string::npos);
}

TEST(UseAfterMoveContract, DialogManagerComputesBlockedTotalCountBeforeMovingBlockedVector) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/DialogManager.cpp");
  auto region = extract_source_region(source, "case telegram_api::contacts_blocked::ID: {",
                                      "case telegram_api::contacts_blockedSlice::ID: {");
  auto normalized = normalize_for_contract(region);

  auto total_count_pos = normalized.find("autototal_count=narrow_cast<int32>(blocked_peers->blocked_.size());");
  auto move_pos = normalized.find("std::move(blocked_peers->blocked_)");

  ASSERT_TRUE(total_count_pos != td::string::npos);
  ASSERT_TRUE(move_pos != td::string::npos);
  ASSERT_TRUE(total_count_pos < move_pos);
  ASSERT_TRUE(
      normalized.find("on_get_blocked_dialogs(offset_,limit_,total_count,std::move(blocked_peers->blocked_),") !=
      td::string::npos);
}

TEST(UseAfterMoveContract, MessagesManagerComputesDialogTotalCountBeforeMovingDialogsVector) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_source_region(source, "case telegram_api::messages_dialogs::ID: {",
                                      "case telegram_api::messages_dialogsSlice::ID: {");
  auto normalized = normalize_for_contract(region);

  auto total_count_pos = normalized.find("autototal_count=narrow_cast<int32>(dialogs->dialogs_.size());");
  auto move_pos = normalized.find("std::move(dialogs->dialogs_)");

  ASSERT_TRUE(total_count_pos != td::string::npos);
  ASSERT_TRUE(move_pos != td::string::npos);
  ASSERT_TRUE(total_count_pos < move_pos);
  ASSERT_TRUE(normalized.find("on_get_dialogs(folder_id_,std::move(dialogs->dialogs_),total_count,") !=
              td::string::npos);
}

TEST(UseAfterMoveContract, RecentDialogListLambdaDoesNotDependOnMovedDialogIdsParameter) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/RecentDialogList.cpp");
  auto region = extract_source_region(source, "td_->messages_manager_->load_dialogs(",
                                      "td_->messages_manager_->get_dialogs_from_list(");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("[promise=mpas.get_promise()](vector<DialogId>)mutable{promise.set_value(Unit());}") !=
              td::string::npos);
}

TEST(UseAfterMoveContract, SecretChatActorComputesFatalExpectationBeforeMovingError) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/SecretChatActor.cpp");
  auto region = extract_source_region(source, "} else if (error.code() != 429) {",
                                      "auto query = create_net_query(*state->message);");
  auto normalized = normalize_for_contract(region);

  auto expected_pos = normalized.find(
      "autois_expected=(error.code()==400&&error.message()==\"ENCRYPTION_DECLINED\")||"
      "error.code()==403;");
  auto fatal_pos = normalized.find("on_fatal_error(std::move(error),is_expected);");

  ASSERT_TRUE(expected_pos != td::string::npos);
  ASSERT_TRUE(fatal_pos != td::string::npos);
  ASSERT_TRUE(expected_pos < fatal_pos);
}

TEST(UseAfterMoveContract, StickersManagerBuildsInputMediaWithoutTernaryDualMoveExpression) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/StickersManager.cpp");
  auto region =
      extract_source_region(source, "bool had_input_file = input_file != nullptr;", "CHECK(input_media != nullptr);");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("autoinput_media=[&]{") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(file_type==FileType::Sticker){") != td::string::npos);
  ASSERT_TRUE(
      normalized.find("returnget_input_media(file_upload_id.get_file_id(),std::move(input_file),nullptr,string());") !=
      td::string::npos);
  ASSERT_TRUE(normalized.find("returntd_->documents_manager_->get_input_media(file_upload_id.get_file_id(),"
                              "std::move(input_file),nullptr);") != td::string::npos);
}

}  // namespace
