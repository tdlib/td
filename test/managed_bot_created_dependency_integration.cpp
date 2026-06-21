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
    if (auto byte = static_cast<unsigned char>(c); byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

TEST(ManagedBotCreatedDependencyIntegration, DependencyMinUserIdAndApiObjectSurfacesStayAligned) {
  auto source = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/MessageContent.cpp"));

  ASSERT_NE(td::string::npos,
            source.find("MessageContentType::ManagedBotCreated:{constauto*content=static_cast<const"
                        "MessageManagedBotCreated*>(message_content);add_managed_bot_created_dependencies("
                        "dependencies,content->bot_user_id);break;}"));
  ASSERT_NE(td::string::npos,
            source.find("MessageContentType::ManagedBotCreated:{constauto*content=static_cast<const"
                        "MessageManagedBotCreated*>(message_content);returnget_managed_bot_created_min_user_ids("
                        "content->bot_user_id);}"));
  ASSERT_NE(td::string::npos,
            source.find("returntd_api::make_object<td_api::messageManagedBotCreated>(td->user_manager_->"
                        "get_user_id_object(m->bot_user_id,\"messageManagedBotCreated\"));"));
}

}  // namespace
