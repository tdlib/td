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

TEST(UseAfterMoveIntegration, AllGuardPatternsExistAcrossDeferredFiles) {
  struct Expectation {
    td::Slice path;
    td::Slice required_snippet;
  };

  const Expectation expectations[] = {
      {"td/telegram/CommonDialogManager.cpp", "autototal_count=narrow_cast<int32>(chats->chats_.size());"},
      {"td/telegram/DialogManager.cpp", "autototal_count=narrow_cast<int32>(blocked_peers->blocked_.size());"},
      {"td/telegram/MessagesManager.cpp", "autototal_count=narrow_cast<int32>(dialogs->dialogs_.size());"},
      {"td/telegram/RecentDialogList.cpp",
       "[promise=mpas.get_promise()](vector<DialogId>)mutable{promise.set_value(Unit());}"},
      {"td/telegram/SecretChatActor.cpp",
       "autois_expected=(error.code()==400&&error.message()==\"ENCRYPTION_DECLINED\")||"
       "error.code()==403;"},
      {"td/telegram/StickersManager.cpp", "autoinput_media=[&]{"},
  };

  for (const auto &expectation : expectations) {
    auto source = td::mtproto::test::read_repo_text_file(expectation.path);
    auto normalized = normalize_for_contract(source);
    ASSERT_TRUE(normalized.find(expectation.required_snippet.str()) != td::string::npos);
  }
}

TEST(UseAfterMoveIntegration, LegacyUnsafePatternsAreAbsentAcrossDeferredFiles) {
  struct Forbidden {
    td::Slice path;
    td::Slice forbidden_snippet;
  };

  const Forbidden forbidden[] = {
      {"td/telegram/CommonDialogManager.cpp",
       "on_get_common_dialogs(user_id_,offset_chat_id_,std::move(chats->chats_),"
       "narrow_cast<int32>(chats->chats_.size()));"},
      {"td/telegram/DialogManager.cpp",
       "on_get_blocked_dialogs(offset_,limit_,narrow_cast<int32>(blocked_peers->blocked_.size()),"
       "std::move(blocked_peers->blocked_),"},
      {"td/telegram/MessagesManager.cpp",
       "on_get_dialogs(folder_id_,std::move(dialogs->dialogs_),narrow_cast<int32>(dialogs->dialogs_.size()),"},
      {"td/telegram/RecentDialogList.cpp", "[promise=mpas.get_promise()](vector<DialogId>dialog_ids)mutable"},
      {"td/telegram/SecretChatActor.cpp",
       "on_fatal_error(std::move(error),(error.code()==400&&error.message()==\"ENCRYPTION_DECLINED\")||"
       "error.code()==403);"},
      {"td/telegram/StickersManager.cpp",
       "file_type==FileType::Sticker?get_input_media(file_upload_id.get_file_id(),std::move(input_file),"
       "nullptr,string()):td_->documents_manager_->get_input_media(file_upload_id.get_file_id(),"
       "std::move(input_file),nullptr)"},
  };

  for (const auto &check : forbidden) {
    auto source = td::mtproto::test::read_repo_text_file(check.path);
    auto normalized = normalize_for_contract(source);
    ASSERT_TRUE(normalized.find(check.forbidden_snippet.str()) == td::string::npos);
  }
}

}  // namespace
