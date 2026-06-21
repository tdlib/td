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

TEST(ParseDialogRepairRefetchAdversarial, RepairMustNotRegressToDialogOnlyReload) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto region = extract_region(source, "if (!dependencies.resolve_force(td_, source)) {",
                               "if (td_->auth_manager_->is_bot()) {");
  auto normalized = normalize_for_contract(region);

  ASSERT_NE(td::string::npos,
            normalized.find("caseDialogDependencyRepairOperation::Type::RefetchMessage:get_message_from_server("
                            "operation.message_full_id,Auto(),source);break;"));
  ASSERT_EQ(td::string::npos,
            normalized.find("for(constauto&operation:make_dialog_dependency_repair_operations(dialog_id,"
                            "unresolved_message_ids)){caseDialogDependencyRepairOperation::Type::RequeryDialog:"
                            "send_get_dialog_query(dialog_id,Auto(),0,source);break;}"));
}

}  // namespace
