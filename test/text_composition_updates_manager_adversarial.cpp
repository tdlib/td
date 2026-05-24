// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_updates_manager_test_utils.h"

TEST(TextCompositionUpdatesManagerAdversarial, BridgeMustNotUseDirectCrossActorCall) {
  auto body = td::w5_text_composition_updates_manager_test::ai_compose_styles_bridge_body();

  ASSERT_TRUE(!body.contains("td_->translation_manager_->on_update_ai_compose_styles("));
}

TEST(TextCompositionUpdatesManagerAdversarial, BridgeMustNotDropPromiseOnForwardPath) {
  auto body = td::w5_text_composition_updates_manager_test::ai_compose_styles_bridge_body();

  ASSERT_TRUE(
      !body.contains("send_closure(td_->translation_manager_actor_,static_cast<void(TranslationManager::*)(vector<"
                     "string>&&,Promise<Unit>&&)>(&TranslationManager::on_update_ai_compose_styles),std::"
                     "move(ai_compose_styles),Promise<Unit>());"));
}

TEST(TextCompositionUpdatesManagerAdversarial, BridgeMustNotDoubleResolvePromise) {
  auto body = td::w5_text_composition_updates_manager_test::ai_compose_styles_bridge_body();

  ASSERT_EQ(1u, td::w5_text_composition_updates_manager_test::count_occurrences(body, "promise.set_value(Unit());"));
  ASSERT_EQ(1u, td::w5_text_composition_updates_manager_test::count_occurrences(body, "std::move(promise)"));
}

TEST(TextCompositionUpdatesManagerAdversarial, BridgeMustNotContainBareReturnPath) {
  auto body = td::w5_text_composition_updates_manager_test::ai_compose_styles_bridge_body();

  ASSERT_TRUE(!body.contains("return;"));
}

TEST(TextCompositionUpdatesManagerAdversarial, ConcreteTonesHandlerMustNotProxyThroughStylesBridge) {
  auto body = td::w5_text_composition_updates_manager_test::ai_compose_tones_update_body();

  ASSERT_TRUE(!body.contains("on_update_ai_compose_styles("));
}

TEST(TextCompositionUpdatesManagerAdversarial, ConcreteTonesHandlerMustNotUseCrossActorForwarding) {
  auto body = td::w5_text_composition_updates_manager_test::ai_compose_tones_update_body();

  ASSERT_TRUE(!body.contains("send_closure("));
}

TEST(TextCompositionUpdatesManagerAdversarial, ConcreteTonesHandlerMustGuardBotSessionsBeforeReload) {
  auto body = td::w5_text_composition_updates_manager_test::ai_compose_tones_update_body();

  auto bot_guard_pos = body.find(td::w5_text_composition_updates_manager_test::ai_compose_tones_update_bot_guard());
  auto reload_pos = body.find(td::w5_text_composition_updates_manager_test::ai_compose_tones_update_reload_call());

  ASSERT_TRUE(bot_guard_pos != td::string::npos);
  ASSERT_TRUE(reload_pos != td::string::npos);
  ASSERT_TRUE(bot_guard_pos < reload_pos);
}

TEST(TextCompositionUpdatesManagerAdversarial, ConcreteTonesHandlerMustResolvePromiseForBotAndUserPaths) {
  auto body = td::w5_text_composition_updates_manager_test::ai_compose_tones_update_body();

  // Both bot and user paths share a single lambda passed to dispatch_ai_compose_tones_update as the
  // complete_promise argument.  The promise completion therefore appears once explicitly in the
  // UpdatesManager body; the two-path guarantee is maintained inside the dispatch helper and is
  // independently verified by TextCompositionRuntimeHarness tests.
  ASSERT_EQ(1u, td::w5_text_composition_updates_manager_test::count_occurrences(
                    body, td::w5_text_composition_updates_manager_test::ai_compose_tones_update_promise_completion()));
}

TEST(TextCompositionUpdatesManagerAdversarial, ConcreteTonesHandlerMustReloadExactlyOnce) {
  auto body = td::w5_text_composition_updates_manager_test::ai_compose_tones_update_body();

  ASSERT_EQ(1u, td::w5_text_composition_updates_manager_test::count_occurrences(
                    body, td::w5_text_composition_updates_manager_test::ai_compose_tones_update_reload_call()));
}

TEST(TextCompositionUpdatesManagerAdversarial, ConcreteTonesHandlerMustNotContainBareReturnPath) {
  auto body = td::w5_text_composition_updates_manager_test::ai_compose_tones_update_body();

  ASSERT_TRUE(!body.contains("return;"));
}
