// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/Session.h"

#include "td/utils/tests.h"

namespace {

TEST(SessionAuthRecoveryLightFuzz, RetryBudgetNeverExceedsCapAcrossSlidingWindowMatrix) {
  for (td::uint32 seed = 1; seed <= 128; seed++) {
    td::Session::BindKeyFailureState failure_state;
    auto tmp_auth_key_id = static_cast<td::uint64>(seed + 1000);
    auto now = static_cast<double>(seed);
    int observed_retry_count = 0;

    for (int attempt = 0; attempt < 12; attempt++) {
      auto decision = td::Session::note_bind_key_failure(failure_state, tmp_auth_key_id, now);
      if (decision.drop_tmp_auth_key) {
        observed_retry_count = 0;
      } else {
        observed_retry_count = decision.state.retry_count;
        ASSERT_TRUE(observed_retry_count >= 1 && observed_retry_count <= 4);
      }
      failure_state = decision.state;
      now += 61.0;
    }

    ASSERT_TRUE(observed_retry_count >= 0 && observed_retry_count <= 4);
  }
}

TEST(SessionAuthRecoveryLightFuzz, DeferredRecoveryWindowStaysBoundedFromBelow) {
  td::Session::MainKeyCheckFailureState failure_state;
  for (td::uint32 seed = 0; seed < 64; seed++) {
    auto now = 10.0 * static_cast<double>(seed + 1);
    failure_state = td::Session::note_main_key_check_failure(failure_state, now);
    ASSERT_TRUE(failure_state.next_retry_at >= now + 60.0);
    if (td::Session::should_drop_main_auth_key_after_check_failure(failure_state)) {
      break;
    }
  }
}

}  // namespace