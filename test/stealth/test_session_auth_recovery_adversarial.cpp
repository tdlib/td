// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/Session.h"

#include "td/utils/tests.h"

namespace {

TEST(SessionAuthRecoveryAdversarial, RetryCadenceIncreasesBeforeRotation) {
  td::Session::BindKeyFailureState failure_state;

  auto first = td::Session::note_bind_key_failure(failure_state, 41, 10.0);
  ASSERT_FALSE(first.drop_tmp_auth_key);
  ASSERT_EQ(first.state.retry_count, 1);
  ASSERT_TRUE(first.state.retry_at > 10.0);

  auto second = td::Session::note_bind_key_failure(first.state, 41, first.state.retry_at);
  ASSERT_FALSE(second.drop_tmp_auth_key);
  ASSERT_EQ(second.state.retry_count, 2);
  ASSERT_TRUE(second.state.retry_at > first.state.retry_at);

  auto third = td::Session::note_bind_key_failure(second.state, 41, second.state.retry_at);
  ASSERT_FALSE(third.drop_tmp_auth_key);
  ASSERT_EQ(third.state.retry_count, 3);
  ASSERT_TRUE(third.state.retry_at > second.state.retry_at);
}

TEST(SessionAuthRecoveryAdversarial, RepeatedFailuresRotateEphemeralCredential) {
  td::Session::BindKeyFailureState failure_state;
  auto now = 20.0;
  for (int retry = 0; retry < 4; retry++) {
    auto decision = td::Session::note_bind_key_failure(failure_state, 77, now);
    ASSERT_FALSE(decision.drop_tmp_auth_key);
    failure_state = decision.state;
    now = decision.state.retry_at;
  }

  auto final_decision = td::Session::note_bind_key_failure(failure_state, 77, now);
  ASSERT_TRUE(final_decision.drop_tmp_auth_key);
  ASSERT_EQ(final_decision.state.tmp_auth_key_id, static_cast<td::uint64>(0));
  ASSERT_EQ(final_decision.state.retry_count, 0);
  ASSERT_EQ(final_decision.state.retry_at, 0.0);
}

TEST(SessionAuthRecoveryAdversarial, CredentialChangeResetsRetryBudget) {
  td::Session::BindKeyFailureState failure_state;
  auto first = td::Session::note_bind_key_failure(failure_state, 5, 100.0);
  auto reset = td::Session::note_bind_key_failure(first.state, 6, 101.0);

  ASSERT_FALSE(reset.drop_tmp_auth_key);
  ASSERT_EQ(reset.state.tmp_auth_key_id, static_cast<td::uint64>(6));
  ASSERT_EQ(reset.state.retry_count, 1);
  ASSERT_EQ(reset.state.window_started_at, 101.0);
  ASSERT_TRUE(reset.state.retry_at > 101.0);
}

TEST(SessionAuthRecoveryAdversarial, RetryBudgetResetsAfterWindowExpiry) {
  td::Session::BindKeyFailureState failure_state;
  auto first = td::Session::note_bind_key_failure(failure_state, 9, 10.0);
  auto second = td::Session::note_bind_key_failure(first.state, 9, 611.0);

  ASSERT_FALSE(second.drop_tmp_auth_key);
  ASSERT_EQ(second.state.retry_count, 1);
  ASSERT_EQ(second.state.window_started_at, 611.0);
  ASSERT_TRUE(second.state.retry_at > 611.0);
}

TEST(SessionAuthRecoveryAdversarial, RepeatedRecoveryFailuresRequireSecondObservationForTerminalState) {
  td::Session::MainKeyCheckFailureState failure_state;

  failure_state = td::Session::note_main_key_check_failure(failure_state, 50.0);
  ASSERT_FALSE(td::Session::should_drop_main_auth_key_after_check_failure(failure_state));
  ASSERT_EQ(failure_state.failure_count, 1);
  auto first_retry_at = failure_state.next_retry_at;
  ASSERT_TRUE(first_retry_at >= 110.0);

  failure_state = td::Session::note_main_key_check_failure(failure_state, first_retry_at);
  ASSERT_TRUE(td::Session::should_drop_main_auth_key_after_check_failure(failure_state));
  ASSERT_EQ(failure_state.failure_count, 2);
  ASSERT_TRUE(failure_state.next_retry_at > first_retry_at);
}

TEST(SessionAuthRecoveryAdversarial, RetryWindowBlocksImmediateReplay) {
  td::Session::BindKeyFailureState failure_state;
  auto decision = td::Session::note_bind_key_failure(failure_state, 15, 30.0);

  ASSERT_FALSE(
      td::Session::resolve_need_send_bind_key(true, false, 15, 0, decision.state, decision.state.retry_at - 0.1));
  ASSERT_TRUE(td::Session::resolve_need_send_bind_key(true, false, 15, 0, decision.state, decision.state.retry_at));
}

}  // namespace