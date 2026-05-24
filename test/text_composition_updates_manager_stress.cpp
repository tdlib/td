// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_updates_manager_test_utils.h"

TEST(TextCompositionUpdatesManagerStress, RepeatedSourceReadsKeepBridgeContractsStable) {
  td::uint64 iterations = 0;
  td::uint64 contract_hits = 0;

  for (td::int32 i = 0; i < 2000; i++) {
    auto normalized_tl = td::w5_text_composition_updates_manager_test::normalized_telegram_api_tl();
    auto normalized_h = td::w5_text_composition_updates_manager_test::normalized_updates_manager_h();
    auto normalized_cpp = td::w5_text_composition_updates_manager_test::normalized_updates_manager_cpp();
    auto bridge_body = td::w5_text_composition_updates_manager_test::ai_compose_styles_bridge_body();
    auto tones_update_body = td::w5_text_composition_updates_manager_test::ai_compose_tones_update_body();
    auto has_contract =
        normalized_tl.contains(td::w5_text_composition_updates_manager_test::ai_compose_tones_update_constructor()) &&
        normalized_h.contains(td::w5_text_composition_updates_manager_test::ai_compose_styles_bridge_declaration()) &&
        normalized_h.contains(td::w5_text_composition_updates_manager_test::ai_compose_tones_update_declaration()) &&
        normalized_cpp.contains(td::w5_text_composition_updates_manager_test::ai_compose_styles_bridge_signature()) &&
        normalized_cpp.contains(td::w5_text_composition_updates_manager_test::ai_compose_tones_update_signature()) &&
        bridge_body.contains(td::w5_text_composition_updates_manager_test::ai_compose_styles_bridge_bot_guard()) &&
        bridge_body.contains(td::w5_text_composition_updates_manager_test::ai_compose_styles_bridge_dispatch_call()) &&
        td::w5_text_composition_updates_manager_test::count_occurrences(bridge_body, "promise.set_value(Unit());") ==
            1u &&
        td::w5_text_composition_updates_manager_test::count_occurrences(bridge_body, "std::move(promise)") == 1u &&
        tones_update_body.contains(td::w5_text_composition_updates_manager_test::ai_compose_tones_update_bot_guard()) &&
        tones_update_body.contains(
            td::w5_text_composition_updates_manager_test::ai_compose_tones_update_reload_call()) &&
        tones_update_body.contains(
            td::w5_text_composition_updates_manager_test::ai_compose_tones_update_promise_completion()) &&
        tones_update_body.find(td::w5_text_composition_updates_manager_test::ai_compose_tones_update_bot_guard()) <
            tones_update_body.find(
                td::w5_text_composition_updates_manager_test::ai_compose_tones_update_reload_call()) &&
        td::w5_text_composition_updates_manager_test::count_occurrences(
            tones_update_body, td::w5_text_composition_updates_manager_test::ai_compose_tones_update_reload_call()) ==
            1u &&
        td::w5_text_composition_updates_manager_test::count_occurrences(
            tones_update_body,
            td::w5_text_composition_updates_manager_test::ai_compose_tones_update_promise_completion()) == 1u &&
        !bridge_body.contains("return;") &&
        !bridge_body.contains("td_->translation_manager_->on_update_ai_compose_styles(") &&
        !tones_update_body.contains("send_closure(") && !tones_update_body.contains("on_update_ai_compose_styles(") &&
        !tones_update_body.contains("return;");
    contract_hits += has_contract ? 1u : 0u;

    iterations++;
  }

  ASSERT_EQ(iterations, contract_hits);
}
