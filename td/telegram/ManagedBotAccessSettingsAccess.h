// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/telegram/UserId.h"

#include "td/utils/Status.h"

#include <utility>

namespace td {

enum class ManagedBotAccessSettingsAccessResult : std::uint8_t {
  RejectedNonBotSession,
  RejectedTargetLookupError,
  RejectedUnownedBot,
  DelegatedToManager,
};

template <class PromiseT, class RejectAccess, class LoadBotData, class DelegateToManager>
ManagedBotAccessSettingsAccessResult dispatch_managed_bot_access_settings_read(
    bool is_bot_session, int64 bot_user_id, PromiseT &&promise, RejectAccess &&reject_access,
    LoadBotData &&load_bot_data, DelegateToManager &&delegate_to_manager) {
  if (!is_bot_session) {
    reject_access(std::forward<PromiseT>(promise), Status::Error(400, "Only bots can use the method"));
    return ManagedBotAccessSettingsAccessResult::RejectedNonBotSession;
  }

  auto managed_bot_user_id = UserId(bot_user_id);
  auto bot_data = load_bot_data(managed_bot_user_id);
  if (bot_data.is_error()) {
    reject_access(std::forward<PromiseT>(promise), bot_data.move_as_error());
    return ManagedBotAccessSettingsAccessResult::RejectedTargetLookupError;
  }

  if (!bot_data.ok().can_be_edited) {
    reject_access(std::forward<PromiseT>(promise), Status::Error(400, "Bot must be owned"));
    return ManagedBotAccessSettingsAccessResult::RejectedUnownedBot;
  }

  delegate_to_manager(managed_bot_user_id, std::forward<PromiseT>(promise));
  return ManagedBotAccessSettingsAccessResult::DelegatedToManager;
}

template <class SettingsT, class PromiseT, class RejectAccess, class LoadBotData, class DelegateToManager>
ManagedBotAccessSettingsAccessResult dispatch_managed_bot_access_settings_write(
    bool is_bot_session, int64 bot_user_id, SettingsT &&settings, PromiseT &&promise, RejectAccess &&reject_access,
    LoadBotData &&load_bot_data, DelegateToManager &&delegate_to_manager) {
  if (!is_bot_session) {
    reject_access(std::forward<PromiseT>(promise), Status::Error(400, "Only bots can use the method"));
    return ManagedBotAccessSettingsAccessResult::RejectedNonBotSession;
  }

  auto managed_bot_user_id = UserId(bot_user_id);
  auto bot_data = load_bot_data(managed_bot_user_id);
  if (bot_data.is_error()) {
    reject_access(std::forward<PromiseT>(promise), bot_data.move_as_error());
    return ManagedBotAccessSettingsAccessResult::RejectedTargetLookupError;
  }

  if (!bot_data.ok().can_be_edited) {
    reject_access(std::forward<PromiseT>(promise), Status::Error(400, "Bot must be owned"));
    return ManagedBotAccessSettingsAccessResult::RejectedUnownedBot;
  }

  delegate_to_manager(managed_bot_user_id, std::forward<SettingsT>(settings), std::forward<PromiseT>(promise));
  return ManagedBotAccessSettingsAccessResult::DelegatedToManager;
}

}  // namespace td
