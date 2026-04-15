// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/Session.h"

#include "td/utils/tests.h"

namespace {

using RecoveryAction = td::Session::EncryptedMessageInvalidAction;

TEST(SessionAuthRecoveryContract, RecentProtectionWindowSuppressesEscalation) {
  ASSERT_TRUE(td::Session::resolve_encrypted_message_invalid_action(true, true) == RecoveryAction::Ignore);
}

TEST(SessionAuthRecoveryContract, ProtectedStateStartsRecoveryPath) {
  ASSERT_TRUE(td::Session::resolve_encrypted_message_invalid_action(true, false) == RecoveryAction::StartMainKeyCheck);
}

TEST(SessionAuthRecoveryContract, UnprotectedStateUsesTerminalRecovery) {
  ASSERT_TRUE(td::Session::resolve_encrypted_message_invalid_action(false, false) == RecoveryAction::DropMainAuthKey);
}

TEST(SessionAuthRecoveryContract, FreshCredentialAllowsImmediateRetry) {
  td::Session::BindKeyFailureState failure_state;
  ASSERT_TRUE(td::Session::resolve_need_send_bind_key(true, false, 17, 0, failure_state, 100.0));
}

TEST(SessionAuthRecoveryContract, DeferredRecoveryRespectsRetryWindow) {
  td::Session::MainKeyCheckFailureState failure_state;
  failure_state.next_retry_at = 160.0;
  ASSERT_FALSE(td::Session::resolve_need_send_check_main_key(true, 99, 0, failure_state, 159.0));
  ASSERT_TRUE(td::Session::resolve_need_send_check_main_key(true, 99, 0, failure_state, 160.0));
}

}  // namespace