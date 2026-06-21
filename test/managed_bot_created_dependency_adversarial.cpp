// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <string_view>

namespace {

td::string extract_region(std::string_view source, td::Slice begin_marker, td::Slice end_marker) {
  auto begin = source.find(begin_marker.str());
  CHECK(begin != td::string::npos);
  auto end = source.find(end_marker.str(), begin + begin_marker.size());
  CHECK(end != td::string::npos);
  CHECK(end > begin);
  return td::string(source.substr(begin, end - begin));
}

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

TEST(ManagedBotCreatedDependencyAdversarial, DependencyCaseMustNotSilentlyDropBotUserId) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessageContent.cpp");
  auto region = extract_region(
      source, "void add_message_content_dependencies(Dependencies &dependencies, const MessageContent *message_content,",
      "void apply_updates_from_service_message_content(");
  auto normalized = normalize_for_contract(region);

  ASSERT_EQ(td::string::npos, normalized.find("MessageContentType::ManagedBotCreated:break;"));
  ASSERT_EQ(td::string::npos,
            normalized.find("MessageContentType::ManagedBotCreated:{constauto*content=static_cast<const"
                            "MessageManagedBotCreated*>(message_content);break;}"));
  ASSERT_NE(td::string::npos,
            normalized.find("MessageContentType::ManagedBotCreated:{constauto*content=static_cast<const"
                            "MessageManagedBotCreated*>(message_content);add_managed_bot_created_dependencies("
                            "dependencies,content->bot_user_id);break;}"));
}

}  // namespace
