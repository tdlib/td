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

TEST(Wave2UseAfterMoveStress, Wave2ContractsRemainDeterministicAcrossRepeatedValidation) {
  const auto messages_source = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp"));
  const auto common_source = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/CommonDialogManager.cpp"));
  const auto dialog_source = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/DialogManager.cpp"));

  constexpr td::int32 kIterations = 20000;
  for (td::int32 i = 0; i < kIterations; ++i) {
    ASSERT_TRUE(messages_source.find("add_message_to_dialog(to_dialog,std::move(message),false,true,&need_update,"
                                     "&need_update_dialog_pos,\"ForwardMessagesLogEvent\"));") !=
                td::string::npos);
    ASSERT_TRUE(messages_source.find("add_message_to_dialog(d,std::move(message),false,true,&need_update,"
                                     "&need_update_dialog_pos,\"SendQuickReplyShortcutMessagesLogEvent\"));") !=
                td::string::npos);
    ASSERT_TRUE(messages_source.find("\"forwardmessageagain\"") == td::string::npos);
    ASSERT_TRUE(messages_source.find("\"sendquickreplyshortcutmessageagain\"") == td::string::npos);

    ASSERT_TRUE(common_source.find("autototal_count=narrow_cast<int32>(chats->chats_.size());") != td::string::npos);
    ASSERT_TRUE(dialog_source.find("autototal_count=narrow_cast<int32>(blocked_peers->blocked_.size());") !=
                td::string::npos);
  }
}

}  // namespace
