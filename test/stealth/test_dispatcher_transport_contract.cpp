// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/NetQueryDispatcher.h"

#include "td/utils/tests.h"

namespace {

TEST(DispatcherTransportContract, ProtectedModeIgnoresRelaxationRequestForSingleSession) {
  ASSERT_TRUE(td::NetQueryDispatcher::resolve_use_pfs_policy(false, 1));
}

TEST(DispatcherTransportContract, ProtectedModeStaysEnabledWhenExplicitlyRequested) {
  ASSERT_TRUE(td::NetQueryDispatcher::resolve_use_pfs_policy(true, 1));
}

TEST(DispatcherTransportContract, ProtectedModePersistsAcrossParallelSessions) {
  ASSERT_TRUE(td::NetQueryDispatcher::resolve_use_pfs_policy(false, 4));
}

}  // namespace