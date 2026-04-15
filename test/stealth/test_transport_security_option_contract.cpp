// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/OptionManager.h"

#include "td/utils/tests.h"

namespace {

TEST(TransportSecurityOptionContract, RelaxedRequestNormalizesToProtectedMode) {
  ASSERT_TRUE(td::OptionManager::resolve_use_pfs_option_value(false));
}

TEST(TransportSecurityOptionContract, ProtectedRequestStaysProtectedMode) {
  ASSERT_TRUE(td::OptionManager::resolve_use_pfs_option_value(true));
}

}  // namespace