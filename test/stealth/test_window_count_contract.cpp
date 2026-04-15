// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/Handshake.h"

#include "td/utils/tests.h"

namespace {

TEST(WindowCountContract, ProductionMinimumRequiresTwoEntries) {
  ASSERT_EQ(2u, td::mtproto::AuthKeyHandshake::minimum_server_entry_count());
  ASSERT_TRUE(td::mtproto::AuthKeyHandshake::should_warn_on_server_entry_count(1));
  ASSERT_FALSE(td::mtproto::AuthKeyHandshake::should_warn_on_server_entry_count(2));
}

}  // namespace