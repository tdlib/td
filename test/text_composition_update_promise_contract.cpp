// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_update_promise_test_utils.h"

TEST(TextCompositionUpdatePromiseContract, HeaderDeclaresPromiseAwareUpdateOverload) {
  auto normalized = td::w5_text_composition_update_promise_test::normalized_translation_manager_h();

  ASSERT_TRUE(normalized.contains(
      "voidon_update_ai_compose_styles(vector<string>&&ai_compose_styles,Promise<Unit>&&promise);"));
}

TEST(TextCompositionUpdatePromiseContract, CppDefinesPromiseAwareUpdateOverload) {
  auto normalized = td::w5_text_composition_update_promise_test::normalized_translation_manager_cpp();

  ASSERT_TRUE(normalized.contains(td::w5_text_composition_update_promise_test::promise_update_overload_signature()));
}

TEST(TextCompositionUpdatePromiseContract, PromiseAwareOverloadDelegatesToSynchronousUpdatePath) {
  auto body = td::w5_text_composition_update_promise_test::promise_update_overload_body();

  ASSERT_TRUE(body.contains(td::w5_text_composition_update_promise_test::promise_update_delegate_call()));
}

TEST(TextCompositionUpdatePromiseContract, PromiseAwareOverloadCompletesPromiseWithUnit) {
  auto body = td::w5_text_composition_update_promise_test::promise_update_overload_body();

  ASSERT_TRUE(body.contains(td::w5_text_composition_update_promise_test::promise_update_completion_call()));
}
