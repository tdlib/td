// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

namespace {

td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    switch (static_cast<unsigned char>(c)) {
      case ' ':
      case '\t':
      case '\r':
      case '\n':
        continue;
      default:
        break;
    }
    normalized.push_back(c);
  }
  return normalized;
}

td::string read_normalized(td::Slice path) {
  return normalize_for_contract(td::mtproto::test::read_repo_text_file(path));
}

TEST(Wave2UseAfterMoveContract, CommonDialogsCapturesTotalCountBeforeMovingChatsVector) {
  auto source = read_normalized("td/telegram/CommonDialogManager.cpp");

  auto total_count_pos = source.find("autototal_count=narrow_cast<int32>(chats->chats_.size());");
  auto move_call_pos = source.find(
      "on_get_common_dialogs(user_id_,offset_chat_id_,std::move(chats->chats_),total_count);");

  ASSERT_TRUE(total_count_pos != td::string::npos);
  ASSERT_TRUE(move_call_pos != td::string::npos);
  ASSERT_TRUE(total_count_pos < move_call_pos);
}

TEST(Wave2UseAfterMoveContract, BlockedDialogsCapturesTotalCountBeforeMovingBlockedVector) {
  auto source = read_normalized("td/telegram/DialogManager.cpp");

  auto total_count_pos = source.find("autototal_count=narrow_cast<int32>(blocked_peers->blocked_.size());");
  auto move_call_pos = source.find(
      "on_get_blocked_dialogs(offset_,limit_,total_count,std::move(blocked_peers->blocked_),std::move(promise_));");

  ASSERT_TRUE(total_count_pos != td::string::npos);
  ASSERT_TRUE(move_call_pos != td::string::npos);
  ASSERT_TRUE(total_count_pos < move_call_pos);
}

TEST(Wave2UseAfterMoveContract, DialogListCapturesTotalCountBeforeMovingDialogsVector) {
  auto source = read_normalized("td/telegram/MessagesManager.cpp");

  auto total_count_pos = source.find("autototal_count=narrow_cast<int32>(dialogs->dialogs_.size());");
  auto move_call_pos = source.find(
      "on_get_dialogs(folder_id_,std::move(dialogs->dialogs_),total_count,std::move(dialogs->messages_),std::move(promise_));");

  ASSERT_TRUE(total_count_pos != td::string::npos);
  ASSERT_TRUE(move_call_pos != td::string::npos);
  ASSERT_TRUE(total_count_pos < move_call_pos);
}

TEST(Wave2UseAfterMoveContract, SecretChatFatalErrorClassifiedBeforeMovingStatus) {
  auto source = read_normalized("td/telegram/SecretChatActor.cpp");

  auto classify_pos = source.find(
      "autois_expected=(error.code()==400&&error.message()==\"ENCRYPTION_DECLINED\")||error.code()==403;");
  auto fatal_pos = source.find("returnon_fatal_error(std::move(error),is_expected);");

  ASSERT_TRUE(classify_pos != td::string::npos);
  ASSERT_TRUE(fatal_pos != td::string::npos);
  ASSERT_TRUE(classify_pos < fatal_pos);
}

TEST(Wave2UseAfterMoveContract, StickerUploadInputMediaSelectionUsesSingleMovePerPath) {
  auto source = read_normalized("td/telegram/StickersManager.cpp");

  ASSERT_TRUE(source.find("if(file_type==FileType::Sticker){returnget_input_media(file_upload_id.get_file_id(),"
                          "std::move(input_file),nullptr,string());}"
                          "returntd_->documents_manager_->get_input_media(file_upload_id.get_file_id(),"
                          "std::move(input_file),nullptr);") != td::string::npos);
}

TEST(Wave2UseAfterMoveContract, BinlogReplayUsesCanonicalWave2SourceTags) {
  auto source = read_normalized("td/telegram/MessagesManager.cpp");

  ASSERT_TRUE(source.find("add_message_to_dialog(to_dialog,std::move(message),false,true,&need_update,"
                          "&need_update_dialog_pos,\"ForwardMessagesLogEvent\"));") != td::string::npos);
  ASSERT_TRUE(source.find("add_message_to_dialog(d,std::move(message),false,true,&need_update,"
                          "&need_update_dialog_pos,\"SendQuickReplyShortcutMessagesLogEvent\"));") !=
              td::string::npos);
}

}  // namespace
