// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/Handshake.h"

#include "td/utils/tests.h"

namespace {

TEST(HandshakeFingerprintCardinalityContract, ProductionMinimumRequiresTwoFingerprints) {
  ASSERT_EQ(2u, td::mtproto::AuthKeyHandshake::minimum_server_fingerprint_count());
  ASSERT_TRUE(td::mtproto::AuthKeyHandshake::should_warn_on_server_fingerprint_count(1));
  ASSERT_FALSE(td::mtproto::AuthKeyHandshake::should_warn_on_server_fingerprint_count(2));
}

}  // namespace