// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_update_promise_test_utils.h"

TEST(TextCompositionUpdatePromiseAdversarial, PromiseAwareOverloadMustNotSkipPromiseCompletion) {
  auto body = td::w5_text_composition_update_promise_test::promise_update_overload_body();

  ASSERT_TRUE(body != "on_update_ai_compose_styles(std::move(ai_compose_styles));");
}

TEST(TextCompositionUpdatePromiseAdversarial, PromiseAwareOverloadMustNotBypassSynchronousContractPath) {
  auto body = td::w5_text_composition_update_promise_test::promise_update_overload_body();

  ASSERT_TRUE(body != "promise.set_value(Unit());");
}

TEST(TextCompositionUpdatePromiseAdversarial, PromiseCompletionMustHappenExactlyOnceInPromiseAwareBody) {
  auto body = td::w5_text_composition_update_promise_test::promise_update_overload_body();

  ASSERT_EQ(1u, td::w5_text_composition_update_promise_test::count_occurrences(
                    body, td::w5_text_composition_update_promise_test::promise_update_completion_call()));
}

TEST(TextCompositionUpdatePromiseAdversarial, PromiseAwareBodyMustNotContainEarlyReturn) {
  auto body = td::w5_text_composition_update_promise_test::promise_update_overload_body();

  ASSERT_TRUE(!body.contains("return"));
}
