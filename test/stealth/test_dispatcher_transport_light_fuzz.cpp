// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/NetQueryDispatcher.h"

#include "td/utils/tests.h"

#include <limits>

namespace {

TEST(DispatcherTransportLightFuzz, ProtectedModeIgnoresOptionAndSessionCountMatrix) {
  for (bool option_use_pfs : {false, true}) {
    ASSERT_TRUE(td::NetQueryDispatcher::resolve_use_pfs_policy(option_use_pfs, 0));
    ASSERT_TRUE(td::NetQueryDispatcher::resolve_use_pfs_policy(option_use_pfs, std::numeric_limits<td::int32>::min()));
    ASSERT_TRUE(td::NetQueryDispatcher::resolve_use_pfs_policy(option_use_pfs, std::numeric_limits<td::int32>::max()));

    for (td::uint32 seed = 1; seed <= 256; seed++) {
      auto session_count = static_cast<td::int32>((seed % 2 == 0 ? -1 : 1) * static_cast<td::int32>(seed * 37u));
      ASSERT_TRUE(td::NetQueryDispatcher::resolve_use_pfs_policy(option_use_pfs, session_count));
    }
  }
}

}  // namespace