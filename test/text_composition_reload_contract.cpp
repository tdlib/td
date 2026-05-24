// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_reload_test_utils.h"

TEST(TextCompositionReloadContract, HeaderDeclaresReloadMethod) {
  auto normalized_h = td::w5_text_composition_reload_test::normalized_translation_manager_h();

  ASSERT_TRUE(normalized_h.contains(td::w5_text_composition_reload_test::reload_declaration()));
}

TEST(TextCompositionReloadContract, CppDefinesReloadMethod) {
  auto normalized_cpp = td::w5_text_composition_reload_test::normalized_translation_manager_cpp();

  ASSERT_TRUE(normalized_cpp.contains(td::w5_text_composition_reload_test::reload_signature()));
}

TEST(TextCompositionReloadContract, ReloadDelegatesToConfigManagerRegetAppConfig) {
  auto body = td::w5_text_composition_reload_test::reload_body();

  ASSERT_TRUE(body.contains(td::w5_text_composition_reload_test::reload_dispatch_call()));
}

TEST(TextCompositionReloadContract, ReloadDispatchesExactlyOnce) {
  auto body = td::w5_text_composition_reload_test::reload_body();

  ASSERT_EQ(1u, td::w5_text_composition_reload_test::count_occurrences(
                    body, td::w5_text_composition_reload_test::reload_dispatch_call()));
}