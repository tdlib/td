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

TEST(Wave2UseAfterMoveAdversarial, LegacyReplaySourceLiteralsMustNotReappear) {
  auto source = read_normalized("td/telegram/MessagesManager.cpp");

  ASSERT_TRUE(source.find("\"forwardmessageagain\"") == td::string::npos);
  ASSERT_TRUE(source.find("\"sendquickreplyshortcutmessageagain\"") == td::string::npos);
}

TEST(Wave2UseAfterMoveAdversarial, MoveThenSizeInlinePatternsMustStayRejected) {
  auto common_source = read_normalized("td/telegram/CommonDialogManager.cpp");
  auto dialog_source = read_normalized("td/telegram/DialogManager.cpp");
  auto messages_source = read_normalized("td/telegram/MessagesManager.cpp");

  ASSERT_TRUE(common_source.find("on_get_common_dialogs(user_id_,offset_chat_id_,std::move(chats->chats_),"
                                 "narrow_cast<int32>(chats->chats_.size()));") == td::string::npos);
  ASSERT_TRUE(dialog_source.find("on_get_blocked_dialogs(offset_,limit_,narrow_cast<int32>(blocked_peers->"
                                 "blocked_.size()),std::move(blocked_peers->blocked_),std::move(promise_));") ==
              td::string::npos);
  ASSERT_TRUE(messages_source.find("on_get_dialogs(folder_id_,std::move(dialogs->dialogs_),"
                                   "narrow_cast<int32>(dialogs->dialogs_.size()),std::move(dialogs->messages_),"
                                   "std::move(promise_));") == td::string::npos);
}

TEST(Wave2UseAfterMoveAdversarial, StickerInputMediaMustNotUseConditionalDoubleMoveExpression) {
  auto source = read_normalized("td/telegram/StickersManager.cpp");

  ASSERT_TRUE(source.find("autoinput_media=[&]{returnfile_type==FileType::Sticker?get_input_media(file_upload_id."
                          "get_file_id(),std::move(input_file),nullptr,string()):td_->documents_manager_->"
                          "get_input_media(file_upload_id.get_file_id(),std::move(input_file),nullptr);}();") ==
              td::string::npos);
}

TEST(Wave2UseAfterMoveAdversarial, SecretChatFatalErrorMustNotInlineConditionAfterMove) {
  auto source = read_normalized("td/telegram/SecretChatActor.cpp");

  ASSERT_TRUE(source.find("returnon_fatal_error(std::move(error),(error.code()==400&&error.message()=="
                          "\"ENCRYPTION_DECLINED\")||error.code()==403);") == td::string::npos);
}

}  // namespace
