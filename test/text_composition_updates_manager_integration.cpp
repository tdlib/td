// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_reload_test_utils.h"
#include "test/text_composition_updates_manager_test_utils.h"

TEST(TextCompositionUpdatesManagerIntegration, HeaderAndCppExposeSameBridgeSignature) {
  auto normalized_h = td::w5_text_composition_updates_manager_test::normalized_updates_manager_h();
  auto normalized_cpp = td::w5_text_composition_updates_manager_test::normalized_updates_manager_cpp();

  ASSERT_TRUE(
      normalized_h.contains(td::w5_text_composition_updates_manager_test::ai_compose_styles_bridge_declaration()));
  ASSERT_TRUE(
      normalized_cpp.contains(td::w5_text_composition_updates_manager_test::ai_compose_styles_bridge_signature()));
}

TEST(TextCompositionUpdatesManagerIntegration, BotGuardRunsBeforeCrossActorForwarding) {
  auto body = td::w5_text_composition_updates_manager_test::ai_compose_styles_bridge_body();

  auto bot_guard_pos = body.find(td::w5_text_composition_updates_manager_test::ai_compose_styles_bridge_bot_guard());
  auto dispatch_pos = body.find(td::w5_text_composition_updates_manager_test::ai_compose_styles_bridge_dispatch_call());

  ASSERT_TRUE(bot_guard_pos != td::string::npos);
  ASSERT_TRUE(dispatch_pos != td::string::npos);
  ASSERT_TRUE(bot_guard_pos < dispatch_pos);
}

TEST(TextCompositionUpdatesManagerIntegration, BridgeDispatchesExactlyOnePromiseAwareForward) {
  auto body = td::w5_text_composition_updates_manager_test::ai_compose_styles_bridge_body();

  ASSERT_EQ(1u, td::w5_text_composition_updates_manager_test::count_occurrences(
                    body, td::w5_text_composition_updates_manager_test::ai_compose_styles_bridge_dispatch_call()));
}

TEST(TextCompositionUpdatesManagerIntegration, ConcreteToneUpdateSurfaceIsConsistentAcrossSchemaAndCode) {
  auto normalized_tl = td::w5_text_composition_updates_manager_test::normalized_telegram_api_tl();
  auto normalized_h = td::w5_text_composition_updates_manager_test::normalized_updates_manager_h();
  auto normalized_cpp = td::w5_text_composition_updates_manager_test::normalized_updates_manager_cpp();

  ASSERT_TRUE(
      normalized_tl.contains(td::w5_text_composition_updates_manager_test::ai_compose_tones_update_constructor()));
  ASSERT_TRUE(
      normalized_h.contains(td::w5_text_composition_updates_manager_test::ai_compose_tones_update_declaration()));
  ASSERT_TRUE(
      normalized_cpp.contains(td::w5_text_composition_updates_manager_test::ai_compose_tones_update_signature()));
}

TEST(TextCompositionUpdatesManagerIntegration, ConcreteToneUpdateReloadsBeforePromiseCompletion) {
  auto body = td::w5_text_composition_updates_manager_test::ai_compose_tones_update_body();

  auto reload_pos = body.find(td::w5_text_composition_updates_manager_test::ai_compose_tones_update_reload_call());
  auto promise_pos =
      body.rfind(td::w5_text_composition_updates_manager_test::ai_compose_tones_update_promise_completion());

  ASSERT_TRUE(reload_pos != td::string::npos);
  ASSERT_TRUE(promise_pos != td::string::npos);
  ASSERT_TRUE(reload_pos < promise_pos);
}

TEST(TextCompositionUpdatesManagerIntegration, ConcreteToneUpdateBotGuardRunsBeforeReload) {
  auto body = td::w5_text_composition_updates_manager_test::ai_compose_tones_update_body();

  auto bot_guard_pos = body.find(td::w5_text_composition_updates_manager_test::ai_compose_tones_update_bot_guard());
  auto reload_pos = body.find(td::w5_text_composition_updates_manager_test::ai_compose_tones_update_reload_call());

  ASSERT_TRUE(bot_guard_pos != td::string::npos);
  ASSERT_TRUE(reload_pos != td::string::npos);
  ASSERT_TRUE(bot_guard_pos < reload_pos);
}

TEST(TextCompositionUpdatesManagerIntegration, ConcreteToneUpdateDoesOneReloadAndOnePromiseCompletion) {
  auto body = td::w5_text_composition_updates_manager_test::ai_compose_tones_update_body();

  ASSERT_EQ(1u, td::w5_text_composition_updates_manager_test::count_occurrences(
                    body, td::w5_text_composition_updates_manager_test::ai_compose_tones_update_reload_call()));
  // Both bot and user paths share a single promise-completion lambda passed into
  // dispatch_ai_compose_tones_update.  One explicit occurrence in the UpdatesManager body is correct;
  // the two-path runtime guarantee is verified by TextCompositionRuntimeHarness tests.
  ASSERT_EQ(1u, td::w5_text_composition_updates_manager_test::count_occurrences(
                    body, td::w5_text_composition_updates_manager_test::ai_compose_tones_update_promise_completion()));
}

TEST(TextCompositionUpdatesManagerIntegration, RuntimeDispatchModelReloadsBeforeCompletingUpdatePromise) {
  auto update_body = td::w5_text_composition_updates_manager_test::ai_compose_tones_update_body();
  auto reload_body = td::w5_text_composition_reload_test::reload_body();

  auto update_reload_pos =
      update_body.find(td::w5_text_composition_updates_manager_test::ai_compose_tones_update_reload_call());
  auto update_promise_pos =
      update_body.rfind(td::w5_text_composition_updates_manager_test::ai_compose_tones_update_promise_completion());

  ASSERT_TRUE(update_reload_pos != td::string::npos);
  ASSERT_TRUE(update_promise_pos != td::string::npos);
  ASSERT_TRUE(update_reload_pos < update_promise_pos);

  ASSERT_TRUE(reload_body.contains(td::w5_text_composition_reload_test::reload_dispatch_call()));
  ASSERT_TRUE(reload_body.contains("&ConfigManager::reload_app_config"));
  ASSERT_TRUE(!reload_body.contains("std::move(promise).set_value(Unit());"));
}
