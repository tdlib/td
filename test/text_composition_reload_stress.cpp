// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_reload_test_utils.h"

TEST(TextCompositionReloadStress, RepeatedSourceReadsKeepReloadContractStable) {
  td::uint64 iterations = 0;
  td::uint64 contract_hits = 0;

  for (td::int32 i = 0; i < 2000; i++) {
    auto normalized_h = td::w5_text_composition_reload_test::normalized_translation_manager_h();
    auto normalized_translation = td::w5_text_composition_reload_test::normalized_translation_manager_cpp();
    auto normalized_updates = td::w5_text_composition_reload_test::normalized_updates_manager_cpp();
    auto reload_body = td::w5_text_composition_reload_test::reload_body();
    auto update_body = td::w5_text_composition_reload_test::ai_compose_tones_update_body();

    auto has_contract =
        normalized_h.contains(td::w5_text_composition_reload_test::reload_declaration()) &&
        normalized_translation.contains(td::w5_text_composition_reload_test::reload_signature()) &&
        normalized_updates.contains(td::w5_text_composition_reload_test::ai_compose_tones_update_signature()) &&
        reload_body.contains(td::w5_text_composition_reload_test::reload_dispatch_call()) &&
        !reload_body.contains(td::w5_text_composition_reload_test::reload_noop_call()) &&
        !reload_body.contains("return") && !reload_body.contains("&ConfigManager::request_config") &&
        td::w5_text_composition_reload_test::count_occurrences(
            reload_body, td::w5_text_composition_reload_test::reload_dispatch_call()) == 1u &&
        update_body.contains(td::w5_text_composition_reload_test::ai_compose_tones_update_reload_call()) &&
        update_body.contains(td::w5_text_composition_reload_test::ai_compose_tones_update_promise_completion()) &&
        update_body.find(td::w5_text_composition_reload_test::ai_compose_tones_update_reload_call()) <
            update_body.rfind(td::w5_text_composition_reload_test::ai_compose_tones_update_promise_completion());

    contract_hits += has_contract ? 1u : 0u;
    iterations++;
  }

  ASSERT_EQ(iterations, contract_hits);
}