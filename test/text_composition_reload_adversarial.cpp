// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_reload_test_utils.h"

TEST(TextCompositionReloadAdversarial, ReloadMustNotBeNoopPromiseCompletion) {
  auto body = td::w5_text_composition_reload_test::reload_body();

  ASSERT_TRUE(!body.contains(td::w5_text_composition_reload_test::reload_noop_call()));
}

TEST(TextCompositionReloadAdversarial, ReloadMustNotUseRequestConfigInsteadOfRegetAppConfig) {
  auto body = td::w5_text_composition_reload_test::reload_body();

  ASSERT_TRUE(!body.contains("&ConfigManager::request_config"));
}

TEST(TextCompositionReloadAdversarial, ReloadMustNotContainEarlyReturnPath) {
  auto body = td::w5_text_composition_reload_test::reload_body();

  ASSERT_TRUE(!body.contains("return"));
}

TEST(TextCompositionReloadAdversarial, ReloadMustKeepPromiseMoveInDispatchPath) {
  auto body = td::w5_text_composition_reload_test::reload_body();

  ASSERT_EQ(1u, td::w5_text_composition_reload_test::count_occurrences(body, "std::move(promise)"));
}