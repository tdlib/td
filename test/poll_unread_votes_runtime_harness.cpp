// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/PollUnreadVotesUpdateDispatch.h"

#include "td/utils/Random.h"
#include "td/utils/tests.h"

TEST(PollUnreadVotesRuntimeHarness, UnsupportedMessagesAreIgnoredBeforeTransitionChecks) {
  ASSERT_EQ(static_cast<int>(td::PollUnreadVotesUpdateAction::IgnoredUnsupportedMessage),
            static_cast<int>(td::dispatch_poll_unread_votes_update_action(false, false, true)));
  ASSERT_EQ(static_cast<int>(td::PollUnreadVotesUpdateAction::IgnoredUnsupportedMessage),
            static_cast<int>(td::dispatch_poll_unread_votes_update_action(false, true, false)));
}

TEST(PollUnreadVotesRuntimeHarness, DuplicateStateIsIgnoredForSupportedMessages) {
  ASSERT_EQ(static_cast<int>(td::PollUnreadVotesUpdateAction::IgnoredDuplicateState),
            static_cast<int>(td::dispatch_poll_unread_votes_update_action(true, false, false)));
  ASSERT_EQ(static_cast<int>(td::PollUnreadVotesUpdateAction::IgnoredDuplicateState),
            static_cast<int>(td::dispatch_poll_unread_votes_update_action(true, true, true)));
}

TEST(PollUnreadVotesRuntimeHarness, TransitionToUnreadReturnsAddAction) {
  ASSERT_EQ(static_cast<int>(td::PollUnreadVotesUpdateAction::AddedUnreadVotes),
            static_cast<int>(td::dispatch_poll_unread_votes_update_action(true, false, true)));
}

TEST(PollUnreadVotesRuntimeHarness, TransitionToReadReturnsRemoveAction) {
  ASSERT_EQ(static_cast<int>(td::PollUnreadVotesUpdateAction::RemovedUnreadVotes),
            static_cast<int>(td::dispatch_poll_unread_votes_update_action(true, true, false)));
}

TEST(PollUnreadVotesRuntimeHarness, DeterministicFuzzMatchesTransitionTruthTable) {
  for (int i = 0; i < 10000; i++) {
    const bool is_supported_poll_message = td::Random::fast(0, 1) == 1;
    const bool has_current_unread_votes = td::Random::fast(0, 1) == 1;
    const bool has_target_unread_votes = td::Random::fast(0, 1) == 1;

    auto action = td::dispatch_poll_unread_votes_update_action(is_supported_poll_message, has_current_unread_votes,
                                                               has_target_unread_votes);

    if (!is_supported_poll_message) {
      ASSERT_EQ(static_cast<int>(td::PollUnreadVotesUpdateAction::IgnoredUnsupportedMessage), static_cast<int>(action));
      continue;
    }
    if (has_current_unread_votes == has_target_unread_votes) {
      ASSERT_EQ(static_cast<int>(td::PollUnreadVotesUpdateAction::IgnoredDuplicateState), static_cast<int>(action));
      continue;
    }
    if (has_target_unread_votes) {
      ASSERT_EQ(static_cast<int>(td::PollUnreadVotesUpdateAction::AddedUnreadVotes), static_cast<int>(action));
    } else {
      ASSERT_EQ(static_cast<int>(td::PollUnreadVotesUpdateAction::RemovedUnreadVotes), static_cast<int>(action));
    }
  }
}