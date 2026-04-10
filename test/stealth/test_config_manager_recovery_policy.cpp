// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/ConfigManager.h"

#include "td/utils/tests.h"

namespace {

TEST(ConfigManagerRecoveryPolicy, FirstRecoveryConnectionAttemptIsDispatched) {
  ASSERT_TRUE(td::get_full_config_recovery_connection_action(1) == td::FullConfigRecoveryConnectionAction::Dispatch);
}

TEST(ConfigManagerRecoveryPolicy, SecondRecoveryConnectionAttemptIsDispatched) {
  ASSERT_TRUE(td::get_full_config_recovery_connection_action(2) == td::FullConfigRecoveryConnectionAction::Dispatch);
}

TEST(ConfigManagerRecoveryPolicy, ThirdRecoveryConnectionAttemptIsDelayedForever) {
  ASSERT_TRUE(td::get_full_config_recovery_connection_action(3) ==
              td::FullConfigRecoveryConnectionAction::DelayForever);
}

TEST(ConfigManagerRecoveryPolicy, LargeRecoveryConnectionAttemptCountsStayDelayedForever) {
  ASSERT_TRUE(td::get_full_config_recovery_connection_action(100) ==
              td::FullConfigRecoveryConnectionAction::DelayForever);
}

}  // namespace