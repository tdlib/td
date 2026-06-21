// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
#pragma once

#include "td/telegram/Dependencies.h"
#include "td/telegram/MessageFullId.h"

#include "td/utils/common.h"

namespace td {

inline void add_managed_bot_created_dependencies(Dependencies &dependencies, UserId bot_user_id) {
  dependencies.add(bot_user_id);
}

inline vector<UserId> get_managed_bot_created_min_user_ids(UserId bot_user_id) {
  return {bot_user_id};
}

enum class PendingCallNotificationAction { None, AddDeferred, RemoveImmediate };

inline PendingCallNotificationAction get_pending_call_notification_action(bool is_outgoing, bool is_pending_call,
                                                                          bool has_notification) {
  if (is_outgoing) {
    return PendingCallNotificationAction::None;
  }
  if (is_pending_call) {
    return has_notification ? PendingCallNotificationAction::None : PendingCallNotificationAction::AddDeferred;
  }
  return has_notification ? PendingCallNotificationAction::RemoveImmediate : PendingCallNotificationAction::None;
}

struct DialogDependencyRepairOperation {
  enum class Type { RefetchMessage, RequeryDialog, ReloadFullDialogInfo };

  Type type = Type::RefetchMessage;
  MessageFullId message_full_id;
};

inline vector<DialogDependencyRepairOperation> make_dialog_dependency_repair_operations(
    DialogId dialog_id, const vector<MessageId> &unresolved_message_ids) {
  vector<DialogDependencyRepairOperation> operations;
  operations.reserve(unresolved_message_ids.size() + 2);
  for (auto message_id : unresolved_message_ids) {
    operations.push_back({DialogDependencyRepairOperation::Type::RefetchMessage, MessageFullId(dialog_id, message_id)});
  }
  operations.push_back({DialogDependencyRepairOperation::Type::RequeryDialog, {}});
  operations.push_back({DialogDependencyRepairOperation::Type::ReloadFullDialogInfo, {}});
  return operations;
}

struct AlternativeVideoRepairCandidate {
  int32 duration = 0;
  bool has_thumbnail = false;
};

struct AlternativeVideoRepairPlan {
  int32 repaired_duration = 0;
  bool needs_alternative_thumbnail_scan = false;
};

inline AlternativeVideoRepairPlan get_alternative_video_repair_plan(
    int32 primary_duration, bool has_primary_thumbnail, const vector<AlternativeVideoRepairCandidate> &alternatives) {
  int32 common_duration = 0;
  bool has_alternative_thumbnail = false;
  for (const auto &alternative : alternatives) {
    if (alternative.duration > 0) {
      if (common_duration == 0) {
        common_duration = alternative.duration;
      } else if (common_duration != alternative.duration) {
        common_duration = -1;
      }
    }
    has_alternative_thumbnail = has_alternative_thumbnail || alternative.has_thumbnail;
  }

  auto repaired_duration = primary_duration;
  if (repaired_duration == 0 && common_duration > 0) {
    repaired_duration = common_duration;
  }

  return {repaired_duration, !has_primary_thumbnail && has_alternative_thumbnail};
}

}  // namespace td
