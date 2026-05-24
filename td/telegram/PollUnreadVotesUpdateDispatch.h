// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/utils/common.h"

namespace td {

enum class PollUnreadVotesUpdateAction : std::uint8_t {
  IgnoredUnsupportedMessage,
  IgnoredDuplicateState,
  RemovedUnreadVotes,
  AddedUnreadVotes,
};

inline PollUnreadVotesUpdateAction dispatch_poll_unread_votes_update_action(bool is_supported_poll_message,
                                                                            bool has_current_unread_votes,
                                                                            bool has_target_unread_votes) {
  if (!is_supported_poll_message) {
    return PollUnreadVotesUpdateAction::IgnoredUnsupportedMessage;
  }
  if (has_current_unread_votes == has_target_unread_votes) {
    return PollUnreadVotesUpdateAction::IgnoredDuplicateState;
  }
  return has_target_unread_votes ? PollUnreadVotesUpdateAction::AddedUnreadVotes
                                 : PollUnreadVotesUpdateAction::RemovedUnreadVotes;
}

}  // namespace td