// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/NetQueryDispatcher.h"

#include "td/utils/tests.h"

#include <limits>

namespace {

TEST(DispatcherTransportAdversarial, ZeroSessionCountStillKeepsProtectedMode) {
  ASSERT_TRUE(td::NetQueryDispatcher::resolve_use_pfs_policy(false, 0));
}

TEST(DispatcherTransportAdversarial, NegativeSessionCountStillKeepsProtectedMode) {
  ASSERT_TRUE(td::NetQueryDispatcher::resolve_use_pfs_policy(false, -1));
}

TEST(DispatcherTransportAdversarial, MinimumIntSessionCountStillKeepsProtectedMode) {
  ASSERT_TRUE(td::NetQueryDispatcher::resolve_use_pfs_policy(false, std::numeric_limits<td::int32>::min()));
}

TEST(DispatcherTransportAdversarial, MaximumIntSessionCountStillKeepsProtectedMode) {
  ASSERT_TRUE(td::NetQueryDispatcher::resolve_use_pfs_policy(false, std::numeric_limits<td::int32>::max()));
}

}  // namespace