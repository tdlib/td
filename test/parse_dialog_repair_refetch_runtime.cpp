// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/BackportTestSeams.h"

#include "td/utils/tests.h"

namespace {

TEST(ParseDialogRepairRefetchRuntime, RepairOperationsPreserveMessageOrderBeforeDialogReload) {
  auto dialog_id = td::DialogId(td::UserId(static_cast<td::int64>(42)));
  std::vector<td::MessageId> unresolved_message_ids{
      td::MessageId(td::ServerMessageId(10)),
      td::MessageId(td::ServerMessageId(20)),
      td::MessageId(td::ServerMessageId(30)),
  };

  auto operations = td::make_dialog_dependency_repair_operations(dialog_id, unresolved_message_ids);

  ASSERT_EQ(5u, operations.size());
  ASSERT_TRUE(td::DialogDependencyRepairOperation::Type::RefetchMessage == operations[0].type);
  ASSERT_TRUE(td::DialogDependencyRepairOperation::Type::RefetchMessage == operations[1].type);
  ASSERT_TRUE(td::DialogDependencyRepairOperation::Type::RefetchMessage == operations[2].type);
  ASSERT_TRUE(td::DialogDependencyRepairOperation::Type::RequeryDialog == operations[3].type);
  ASSERT_TRUE(td::DialogDependencyRepairOperation::Type::ReloadFullDialogInfo == operations[4].type);

  ASSERT_EQ(td::MessageFullId(dialog_id, unresolved_message_ids[0]), operations[0].message_full_id);
  ASSERT_EQ(td::MessageFullId(dialog_id, unresolved_message_ids[1]), operations[1].message_full_id);
  ASSERT_EQ(td::MessageFullId(dialog_id, unresolved_message_ids[2]), operations[2].message_full_id);
}

TEST(ParseDialogRepairRefetchRuntime, EmptyMessageSetStillRequeriesDialogAndReloadsInfo) {
  auto operations = td::make_dialog_dependency_repair_operations(
      td::DialogId(td::ChatId(static_cast<td::int64>(77))), {});

  ASSERT_EQ(2u, operations.size());
  ASSERT_TRUE(td::DialogDependencyRepairOperation::Type::RequeryDialog == operations[0].type);
  ASSERT_TRUE(td::DialogDependencyRepairOperation::Type::ReloadFullDialogInfo == operations[1].type);
}

}  // namespace
