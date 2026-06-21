// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/BackportTestSeams.h"

#include "td/utils/tests.h"

namespace {

TEST(ParseDialogRepairRefetchStress, LargeMessageSetsRetainExactOrderAndTailOperations) {
  constexpr std::size_t kMessageCount = 4096;

  auto dialog_id = td::DialogId(td::ChatId(static_cast<td::int64>(9001)));
  std::vector<td::MessageId> unresolved_message_ids;
  unresolved_message_ids.reserve(kMessageCount);
  for (std::size_t i = 0; i < kMessageCount; i++) {
    unresolved_message_ids.emplace_back(td::ServerMessageId(static_cast<td::int32>(i + 1)));
  }

  auto operations = td::make_dialog_dependency_repair_operations(dialog_id, unresolved_message_ids);

  ASSERT_EQ(kMessageCount + 2, operations.size());
  for (std::size_t i = 0; i < kMessageCount; i++) {
    ASSERT_TRUE(td::DialogDependencyRepairOperation::Type::RefetchMessage == operations[i].type);
    ASSERT_EQ(td::MessageFullId(dialog_id, unresolved_message_ids[i]), operations[i].message_full_id);
  }
  ASSERT_TRUE(td::DialogDependencyRepairOperation::Type::RequeryDialog == operations[kMessageCount].type);
  ASSERT_TRUE(td::DialogDependencyRepairOperation::Type::ReloadFullDialogInfo == operations[kMessageCount + 1].type);
}

TEST(ParseDialogRepairRefetchStress, DuplicateMessageIdsArePreservedInsteadOfBeingCollapsed) {
  auto dialog_id = td::DialogId(td::ChannelId(static_cast<td::int64>(777)));
  std::vector<td::MessageId> unresolved_message_ids{
      td::MessageId(td::ServerMessageId(101)),
      td::MessageId(td::ServerMessageId(101)),
      td::MessageId(td::ServerMessageId(202)),
      td::MessageId(td::ServerMessageId(101)),
  };

  auto operations = td::make_dialog_dependency_repair_operations(dialog_id, unresolved_message_ids);

  ASSERT_EQ(unresolved_message_ids.size() + 2, operations.size());
  for (std::size_t i = 0; i < unresolved_message_ids.size(); i++) {
    ASSERT_TRUE(td::DialogDependencyRepairOperation::Type::RefetchMessage == operations[i].type);
    ASSERT_EQ(td::MessageFullId(dialog_id, unresolved_message_ids[i]), operations[i].message_full_id);
  }
}

}  // namespace
