// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/PublicRsaKeySharedMain.h"

#include "td/utils/tests.h"

namespace {

TEST(MainRsaKeyCardinalityContract, ProductionKeySetExpectsExactlyOneFingerprint) {
  ASSERT_EQ(1u, td::PublicRsaKeySharedMain::expected_key_count(false));
  ASSERT_TRUE(td::PublicRsaKeySharedMain::validate_key_count(1, false).is_ok());
}

TEST(MainRsaKeyCardinalityContract, TestKeySetExpectsExactlyOneFingerprint) {
  ASSERT_EQ(1u, td::PublicRsaKeySharedMain::expected_key_count(true));
  ASSERT_TRUE(td::PublicRsaKeySharedMain::validate_key_count(1, true).is_ok());
}

}  // namespace