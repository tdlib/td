// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/Session.h"

#include "td/utils/tests.h"

namespace {

TEST(SessionReauthDelayContract, PendingDestroySuppressesMainKeyGeneration) {
  ASSERT_FALSE(td::Session::resolve_need_create_main_auth_key(true, true, 10.0, 0.0));
}

TEST(SessionReauthDelayContract, BarrierSuppressesImmediateMainKeyGeneration) {
  ASSERT_FALSE(td::Session::resolve_need_create_main_auth_key(false, true, 11.9, 12.0));
  ASSERT_TRUE(td::Session::resolve_need_create_main_auth_key(false, true, 12.0, 12.0));
}

TEST(SessionReauthDelayContract, MissingMainKeyStartsGenerationWithoutBarrier) {
  ASSERT_TRUE(td::Session::resolve_need_create_main_auth_key(false, true, 20.0, 0.0));
}

}  // namespace