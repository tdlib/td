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

TEST(W2UseAfterMoveAdversarial, CommonDialogManagerNoInlineMovedVectorSizeRead) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/CommonDialogManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("on_get_common_dialogs(user_id_,offset_chat_id_,std::move(chats->chats_),"
                              "narrow_cast<int32>(chats->chats_.size()));") == td::string::npos);
}

TEST(W2UseAfterMoveAdversarial, DialogManagerNoInlineMovedBlockedVectorSizeRead) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/DialogManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(
      normalized.find("on_get_blocked_dialogs(offset_,limit_,narrow_cast<int32>(blocked_peers->blocked_.size()),"
                      "std::move(blocked_peers->blocked_),") == td::string::npos);
}

TEST(W2UseAfterMoveAdversarial, MessagesManagerNoInlineMovedDialogsVectorSizeRead) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("on_get_dialogs(folder_id_,std::move(dialogs->dialogs_),"
                              "narrow_cast<int32>(dialogs->dialogs_.size()),") == td::string::npos);
}

TEST(W2UseAfterMoveAdversarial, RecentDialogListNoNamedMovedDialogIdsLambdaParameter) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/RecentDialogList.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("[promise=mpas.get_promise()](vector<DialogId>dialog_ids)mutable") == td::string::npos);
}

TEST(W2UseAfterMoveAdversarial, SecretChatActorNoInlinePredicateWithMovedErrorArgument) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/SecretChatActor.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("on_fatal_error(std::move(error),(error.code()==400&&"
                              "error.message()==\"ENCRYPTION_DECLINED\")||error.code()==403);") == td::string::npos);
}

TEST(W2UseAfterMoveAdversarial, StickersManagerNoTernaryDualMoveOfInputFile) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/StickersManager.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("file_type==FileType::Sticker?get_input_media(file_upload_id.get_file_id(),"
                              "std::move(input_file),nullptr,string()):td_->documents_manager_->get_input_media("
                              "file_upload_id.get_file_id(),std::move(input_file),nullptr)") == td::string::npos);
}

}  // namespace
