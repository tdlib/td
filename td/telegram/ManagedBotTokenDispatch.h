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

enum class ManagedBotTokenDispatchResult : std::uint8_t {
  RejectedNonBotSession,
  DelegatedToManager,
};

template <class PromiseT, class RejectNonBot, class DelegateToManager>
ManagedBotTokenDispatchResult dispatch_managed_bot_token_request(bool is_bot_session, int64 bot_user_id, bool revoke,
                                                                 PromiseT &&promise, RejectNonBot &&reject_non_bot,
                                                                 DelegateToManager &&delegate_to_manager) {
  if (!is_bot_session) {
    reject_non_bot(std::forward<PromiseT>(promise), Status::Error(400, "Only bots can use the method"));
    return ManagedBotTokenDispatchResult::RejectedNonBotSession;
  }

  delegate_to_manager(UserId(bot_user_id), revoke, std::forward<PromiseT>(promise));
  return ManagedBotTokenDispatchResult::DelegatedToManager;
}

}  // namespace td