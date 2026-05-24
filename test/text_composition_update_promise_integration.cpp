// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_update_promise_test_utils.h"

TEST(TextCompositionUpdatePromiseIntegration, HeaderAndCppMustExposeSamePromiseAwareOverload) {
  auto normalized_h = td::w5_text_composition_update_promise_test::normalized_translation_manager_h();
  auto normalized_cpp = td::w5_text_composition_update_promise_test::normalized_translation_manager_cpp();

  ASSERT_TRUE(normalized_h.contains(
      "voidon_update_ai_compose_styles(vector<string>&&ai_compose_styles,Promise<Unit>&&promise);"));
  ASSERT_TRUE(
      normalized_cpp.contains("voidTranslationManager::on_update_ai_compose_styles(vector<string>&&ai_compose_styles,"
                              "Promise<Unit>&&promise){"));
}

TEST(TextCompositionUpdatePromiseIntegration, PromiseAwareBodyMustDelegateThenResolvePromise) {
  auto body = td::w5_text_composition_update_promise_test::promise_update_overload_body();

  auto delegate_pos = body.find(td::w5_text_composition_update_promise_test::promise_update_delegate_call());
  auto resolve_pos = body.find(td::w5_text_composition_update_promise_test::promise_update_completion_call());

  ASSERT_TRUE(delegate_pos != td::string::npos);
  ASSERT_TRUE(resolve_pos != td::string::npos);
  ASSERT_TRUE(delegate_pos < resolve_pos);
}

TEST(TextCompositionUpdatePromiseIntegration, SynchronousUpdatePathKeepsSanitizeAndDeduplicateContract) {
  auto normalized_cpp = td::w5_text_composition_update_promise_test::normalized_translation_manager_cpp();

  ASSERT_TRUE(normalized_cpp.contains(
      "ai_compose_styles=sanitize_ai_compose_styles(std::move(ai_compose_styles),\"config\");"));
  ASSERT_TRUE(normalized_cpp.contains("if(ai_compose_styles==ai_compose_styles_){return;}"));
}
