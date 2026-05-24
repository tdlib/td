// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_updates_manager_test_utils.h"

TEST(TextCompositionUpdatesManagerContract, HeaderDeclaresAiComposeStylesBridge) {
  auto normalized_h = td::w5_text_composition_updates_manager_test::normalized_updates_manager_h();

  ASSERT_TRUE(
      normalized_h.contains(td::w5_text_composition_updates_manager_test::ai_compose_styles_bridge_declaration()));
}

TEST(TextCompositionUpdatesManagerContract, CppDefinesAiComposeStylesBridge) {
  auto normalized_cpp = td::w5_text_composition_updates_manager_test::normalized_updates_manager_cpp();

  ASSERT_TRUE(
      normalized_cpp.contains(td::w5_text_composition_updates_manager_test::ai_compose_styles_bridge_signature()));
}

TEST(TextCompositionUpdatesManagerContract, BridgeRejectsBotSessionsBeforeForwarding) {
  auto body = td::w5_text_composition_updates_manager_test::ai_compose_styles_bridge_body();

  ASSERT_TRUE(body.contains(td::w5_text_composition_updates_manager_test::ai_compose_styles_bridge_bot_guard()));
}

TEST(TextCompositionUpdatesManagerContract, BridgeForwardsPromiseAwareUpdateToTranslationManager) {
  auto body = td::w5_text_composition_updates_manager_test::ai_compose_styles_bridge_body();

  ASSERT_TRUE(body.contains(td::w5_text_composition_updates_manager_test::ai_compose_styles_bridge_dispatch_call()));
}

TEST(TextCompositionUpdatesManagerContract, TelegramApiSchemaDeclaresUpdateAiComposeTonesConstructor) {
  auto normalized_tl = td::w5_text_composition_updates_manager_test::normalized_telegram_api_tl();

  ASSERT_TRUE(
      normalized_tl.contains(td::w5_text_composition_updates_manager_test::ai_compose_tones_update_constructor()));
}

TEST(TextCompositionUpdatesManagerContract, HeaderDeclaresAiComposeTonesConcreteHandler) {
  auto normalized_h = td::w5_text_composition_updates_manager_test::normalized_updates_manager_h();

  ASSERT_TRUE(
      normalized_h.contains(td::w5_text_composition_updates_manager_test::ai_compose_tones_update_declaration()));
}

TEST(TextCompositionUpdatesManagerContract, CppDefinesAiComposeTonesConcreteHandler) {
  auto normalized_cpp = td::w5_text_composition_updates_manager_test::normalized_updates_manager_cpp();

  ASSERT_TRUE(
      normalized_cpp.contains(td::w5_text_composition_updates_manager_test::ai_compose_tones_update_signature()));
}

TEST(TextCompositionUpdatesManagerContract, ConcreteHandlerReloadsAiComposeTones) {
  auto body = td::w5_text_composition_updates_manager_test::ai_compose_tones_update_body();

  ASSERT_TRUE(body.contains(td::w5_text_composition_updates_manager_test::ai_compose_tones_update_reload_call()));
}

TEST(TextCompositionUpdatesManagerContract, ConcreteHandlerRejectsBotSessionsBeforeReload) {
  auto body = td::w5_text_composition_updates_manager_test::ai_compose_tones_update_body();

  ASSERT_TRUE(body.contains(td::w5_text_composition_updates_manager_test::ai_compose_tones_update_bot_guard()));
}

TEST(TextCompositionUpdatesManagerContract, ConcreteHandlerCompletesUpdatePromise) {
  auto body = td::w5_text_composition_updates_manager_test::ai_compose_tones_update_body();

  ASSERT_TRUE(
      body.contains(td::w5_text_composition_updates_manager_test::ai_compose_tones_update_promise_completion()));
}
