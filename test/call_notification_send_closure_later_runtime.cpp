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

TEST(CallNotificationSendClosureLaterRuntime, OutgoingCallNeverMutatesNotificationState) {
  ASSERT_TRUE(td::PendingCallNotificationAction::None == td::get_pending_call_notification_action(true, true, false));
  ASSERT_TRUE(td::PendingCallNotificationAction::None == td::get_pending_call_notification_action(true, false, true));
}

}  // namespace
