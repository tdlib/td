// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_update_promise_test_utils.h"

TEST(TextCompositionUpdatePromiseStress, RepeatedSourceReadsKeepPromiseUpdateContractsStable) {
  td::uint64 iterations = 0;
  td::uint64 contract_hits = 0;

  for (td::int32 i = 0; i < 2000; i++) {
    auto normalized_h = td::w5_text_composition_update_promise_test::normalized_translation_manager_h();
    auto normalized_cpp = td::w5_text_composition_update_promise_test::normalized_translation_manager_cpp();

    if (auto body = td::w5_text_composition_update_promise_test::promise_update_overload_body();
        normalized_h.contains(
            "voidon_update_ai_compose_styles(vector<string>&&ai_compose_styles,Promise<Unit>&&promise);") &&
        normalized_cpp.contains("voidTranslationManager::on_update_ai_compose_styles(vector<string>&&ai_compose_styles,"
                                "Promise<Unit>&&promise){") &&
        body.contains(td::w5_text_composition_update_promise_test::promise_update_delegate_call()) &&
        body.contains(td::w5_text_composition_update_promise_test::promise_update_completion_call()) &&
        !body.contains("return") &&
        normalized_cpp.contains(
            "ai_compose_styles=sanitize_ai_compose_styles(std::move(ai_compose_styles),\"config\");") &&
        normalized_cpp.contains("if(ai_compose_styles==ai_compose_styles_){return;}")) {
      contract_hits++;
    }
    iterations++;
  }

  ASSERT_EQ(iterations, contract_hits);
}
