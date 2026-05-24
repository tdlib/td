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

enum class ManagedBotTokenAccessResult : std::uint8_t {
  RejectedNonBotSession,
  RejectedTargetLookupError,
  RejectedUnownedBot,
  DelegatedToExporter,
};

template <class PromiseT, class RejectAccess, class LoadBotData, class DelegateToExporter>
ManagedBotTokenAccessResult dispatch_managed_bot_token_export(bool is_bot_session, int64 bot_user_id, bool revoke,
                                                              PromiseT &&promise, RejectAccess &&reject_access,
                                                              LoadBotData &&load_bot_data,
                                                              DelegateToExporter &&delegate_to_exporter) {
  if (!is_bot_session) {
    reject_access(std::forward<PromiseT>(promise), Status::Error(400, "Only bots can use the method"));
    return ManagedBotTokenAccessResult::RejectedNonBotSession;
  }

  auto managed_bot_user_id = UserId(bot_user_id);
  auto bot_data = load_bot_data(managed_bot_user_id);
  if (bot_data.is_error()) {
    reject_access(std::forward<PromiseT>(promise), bot_data.move_as_error());
    return ManagedBotTokenAccessResult::RejectedTargetLookupError;
  }

  if (!bot_data.ok().can_be_edited) {
    reject_access(std::forward<PromiseT>(promise), Status::Error(400, "Bot must be owned"));
    return ManagedBotTokenAccessResult::RejectedUnownedBot;
  }

  delegate_to_exporter(managed_bot_user_id, revoke, std::forward<PromiseT>(promise));
  return ManagedBotTokenAccessResult::DelegatedToExporter;
}

}  // namespace td