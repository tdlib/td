// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/BackportTestSeams.h"

#include "td/utils/tests.h"

namespace {

TEST(CallNotificationSendClosureLaterRuntime, PendingIncomingCallWithoutNotificationAddsDeferred) {
  auto action = td::get_pending_call_notification_action(false, true, false);
  ASSERT_TRUE(td::PendingCallNotificationAction::AddDeferred == action);
}

TEST(CallNotificationSendClosureLaterRuntime, NonPendingIncomingCallWithNotificationRemovesImmediately) {
  auto action = td::get_pending_call_notification_action(false, false, true);
  ASSERT_TRUE(td::PendingCallNotificationAction::RemoveImmediate == action);
}

TEST(CallNotificationSendClosureLaterRuntime, PendingIncomingCallWithExistingNotificationStaysNoOp) {
  auto action = td::get_pending_call_notification_action(false, true, true);
  ASSERT_TRUE(td::PendingCallNotificationAction::None == action);
}

TEST(CallNotificationSendClosureLaterRuntime, NonPendingIncomingCallWithoutNotificationStaysNoOp) {
  auto action = td::get_pending_call_notification_action(false, false, false);
  ASSERT_TRUE(td::PendingCallNotificationAction::None == action);
}

TEST(CallNotificationSendClosureLaterRuntime, OutgoingCallNeverMutatesNotificationState) {
  ASSERT_TRUE(td::PendingCallNotificationAction::None == td::get_pending_call_notification_action(true, true, false));
  ASSERT_TRUE(td::PendingCallNotificationAction::None == td::get_pending_call_notification_action(true, false, true));
}

TEST(CallNotificationSendClosureLaterRuntime, TruthTableMatchesLegacyBranchingForAllStateTuples) {
  struct Case {
    bool is_outgoing;
    bool is_pending_call;
    bool has_notification;
    td::PendingCallNotificationAction expected;
  };

  const Case cases[] = {
      {false, true, false, td::PendingCallNotificationAction::AddDeferred},
      {false, true, true, td::PendingCallNotificationAction::None},
      {false, false, false, td::PendingCallNotificationAction::None},
      {false, false, true, td::PendingCallNotificationAction::RemoveImmediate},
      {true, true, false, td::PendingCallNotificationAction::None},
      {true, true, true, td::PendingCallNotificationAction::None},
      {true, false, false, td::PendingCallNotificationAction::None},
      {true, false, true, td::PendingCallNotificationAction::None},
  };

  for (const auto &test_case : cases) {
    ASSERT_TRUE(test_case.expected ==
                td::get_pending_call_notification_action(test_case.is_outgoing, test_case.is_pending_call,
                                                         test_case.has_notification));
  }
}

}  // namespace
