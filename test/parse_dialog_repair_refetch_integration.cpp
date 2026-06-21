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

TEST(ParseDialogRepairRefetchIntegration, MessageRefetchPrecedesDialogReloadSequence) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(source, "if (!dependencies.resolve_force(td_, source)) {",
                               "if (td_->auth_manager_->is_bot()) {");
  auto normalized = normalize_for_contract(region);

  auto plan_pos = normalized.find("make_dialog_dependency_repair_operations(dialog_id,unresolved_message_ids)");
  auto refetch_pos =
      normalized.find("caseDialogDependencyRepairOperation::Type::RefetchMessage:get_message_from_server("
                      "operation.message_full_id,Auto(),source);break;");
  auto dialog_query_pos =
      normalized.find("caseDialogDependencyRepairOperation::Type::RequeryDialog:send_get_dialog_query("
                      "dialog_id,Auto(),0,source);break;");
  auto full_reload_pos = normalized.find("caseDialogDependencyRepairOperation::Type::ReloadFullDialogInfo:td_->"
                                         "dialog_manager_->reload_dialog_info_full(dialog_id,source);break;");

  ASSERT_NE(td::string::npos, plan_pos);
  ASSERT_NE(td::string::npos, refetch_pos);
  ASSERT_NE(td::string::npos, dialog_query_pos);
  ASSERT_NE(td::string::npos, full_reload_pos);
  ASSERT_TRUE(plan_pos < refetch_pos);
  ASSERT_TRUE(refetch_pos < dialog_query_pos);
  ASSERT_TRUE(dialog_query_pos < full_reload_pos);
}

}  // namespace
