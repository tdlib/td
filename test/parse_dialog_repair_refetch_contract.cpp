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

// Upstream tdlib dc73b3ca3 ("Fix repair of loaded from database chats"), adapted as a fork-local
// equivalent that preserves this fork's caller-`source` provenance instead of the upstream hard-coded
// "parse_dialog N" labels.
// Contract: when a dialog loaded from the local database fails forced dependency resolution, repair MUST
// (1) re-fetch every still-unresolved message from the server, (2) re-query the dialog, and (3) reload
// full dialog info. Re-querying the dialog alone (the pre-fix behavior) leaves message-level references
// permanently unresolved.
TEST(ParseDialogRepairRefetchContract, UnresolvedDependenciesTriggerMessageRefetchAndDialogReload) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(source, "if (!dependencies.resolve_force(td_, source)) {",
                               "if (td_->auth_manager_->is_bot()) {");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("vector<MessageId>unresolved_message_ids;") != td::string::npos);
  ASSERT_TRUE(normalized.find("make_dialog_dependency_repair_operations(dialog_id,unresolved_message_ids)") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find("caseDialogDependencyRepairOperation::Type::RefetchMessage:get_message_from_server("
                              "operation.message_full_id,Auto(),source);break;") != td::string::npos);
  ASSERT_TRUE(normalized.find("caseDialogDependencyRepairOperation::Type::RequeryDialog:send_get_dialog_query("
                              "dialog_id,Auto(),0,source);break;") != td::string::npos);
  ASSERT_TRUE(normalized.find("caseDialogDependencyRepairOperation::Type::ReloadFullDialogInfo:td_->dialog_manager_->"
                              "reload_dialog_info_full(dialog_id,source);break;") != td::string::npos);
}

}  // namespace
