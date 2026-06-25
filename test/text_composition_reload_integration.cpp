// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_reload_test_utils.h"

TEST(TextCompositionReloadIntegration, ReloadMethodAndUpdateHandlerAreBothPresent) {
  auto normalized_translation = td::w5_text_composition_reload_test::normalized_translation_manager_cpp();
  auto normalized_updates = td::w5_text_composition_reload_test::normalized_updates_manager_cpp();

  ASSERT_TRUE(normalized_translation.contains(td::w5_text_composition_reload_test::reload_signature()));
  ASSERT_TRUE(normalized_updates.contains(td::w5_text_composition_reload_test::ai_compose_tones_update_signature()));
}

TEST(TextCompositionReloadIntegration, ReloadDispatchAndUpdateReloadCallTravelTogether) {
  auto reload_body = td::w5_text_composition_reload_test::reload_body();
  auto update_body = td::w5_text_composition_reload_test::ai_compose_tones_update_body();

  ASSERT_TRUE(reload_body.contains(td::w5_text_composition_reload_test::reload_dispatch_call()));
  ASSERT_TRUE(update_body.contains(td::w5_text_composition_reload_test::ai_compose_tones_update_reload_call()));
}

TEST(TextCompositionReloadIntegration, UpdateHandlerStillResolvesPromiseAfterSchedulingReload) {
  auto update_body = td::w5_text_composition_reload_test::ai_compose_tones_update_body();

  auto reload_pos = update_body.find(td::w5_text_composition_reload_test::ai_compose_tones_update_reload_call());
  auto resolve_pos =
      update_body.rfind(td::w5_text_composition_reload_test::ai_compose_tones_update_promise_completion());

  ASSERT_TRUE(reload_pos != td::string::npos);
  ASSERT_TRUE(resolve_pos != td::string::npos);
  ASSERT_TRUE(reload_pos < resolve_pos);
}

TEST(TextCompositionReloadIntegration, ReloadContractRequiresRegetAppConfigInConcreteBody) {
  auto reload_body = td::w5_text_composition_reload_test::reload_body();

  ASSERT_TRUE(reload_body.contains("&ConfigManager::reload_app_config"));
}
