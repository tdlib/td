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

TEST(Wave2UseAfterMoveIntegration, ForwardReplayDependencyAndAddPathUseSameCanonicalTag) {
  auto source = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp"));

  auto dependency_pos = source.find("resolve_force(td_,\"ForwardMessagesLogEvent\")");
  auto add_pos = source.find("add_message_to_dialog(to_dialog,std::move(message),false,true,&need_update,"
                             "&need_update_dialog_pos,\"ForwardMessagesLogEvent\"));");

  ASSERT_TRUE(dependency_pos != td::string::npos);
  ASSERT_TRUE(add_pos != td::string::npos);
  ASSERT_TRUE(dependency_pos < add_pos);
}

TEST(Wave2UseAfterMoveIntegration, QuickReplyReplayDependencyAndAddPathUseSameCanonicalTag) {
  auto source = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp"));

  auto dependency_pos = source.find("resolve_force(td_,\"SendQuickReplyShortcutMessagesLogEvent\")");
  auto add_pos = source.find("add_message_to_dialog(d,std::move(message),false,true,&need_update,"
                             "&need_update_dialog_pos,\"SendQuickReplyShortcutMessagesLogEvent\"));");
  auto update_pos = source.find("send_update_chat_last_message(d,\"SendQuickReplyShortcutMessagesLogEvent\");");

  ASSERT_TRUE(dependency_pos != td::string::npos);
  ASSERT_TRUE(add_pos != td::string::npos);
  ASSERT_TRUE(update_pos != td::string::npos);
  ASSERT_TRUE(dependency_pos < add_pos);
  ASSERT_TRUE(add_pos < update_pos);
}

}  // namespace
