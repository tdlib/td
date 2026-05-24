// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_reload_test_utils.h"

namespace w5_text_composition_reload_light_fuzz_detail {

td::uint32 next_seed(td::uint32 seed) {
  return seed * 1664525u + 1013904223u;
}

}  // namespace w5_text_composition_reload_light_fuzz_detail

TEST(TextCompositionReloadLightFuzz, DeterministicLiteralMatrixPreservesReloadInvariants) {
  const td::vector<td::string> header_needles = {td::w5_text_composition_reload_test::reload_declaration()};
  const td::vector<td::string> translation_needles = {td::w5_text_composition_reload_test::reload_signature(),
                                                      td::w5_text_composition_reload_test::reload_dispatch_call(),
                                                      "&ConfigManager::reget_app_config", "std::move(promise)"};
  const td::vector<td::string> updates_needles = {
      td::w5_text_composition_reload_test::ai_compose_tones_update_signature(),
      td::w5_text_composition_reload_test::ai_compose_tones_update_reload_call(),
      td::w5_text_composition_reload_test::ai_compose_tones_update_promise_completion()};
  const td::vector<td::string> translation_forbidden = {
      "voidTranslationManager::reload_ai_compose_tones(Promise<Unit>&&promise){std::move(promise).set_value(Unit());}",
      "&ConfigManager::request_config",
      "voidTranslationManager::reload_ai_compose_tones(Promise<Unit>&&promise){return;}"};

  td::uint32 seed = 0x3D95F215u;

  for (td::int32 i = 0; i < 4096; i++) {
    seed = w5_text_composition_reload_light_fuzz_detail::next_seed(seed);
    auto normalized_h = td::w5_text_composition_reload_test::normalized_translation_manager_h();
    auto normalized_translation = td::w5_text_composition_reload_test::normalized_translation_manager_cpp();
    auto normalized_updates = td::w5_text_composition_reload_test::normalized_updates_manager_cpp();

    const auto &header_needle = header_needles[seed % header_needles.size()];
    seed = w5_text_composition_reload_light_fuzz_detail::next_seed(seed);
    const auto &translation_needle = translation_needles[seed % translation_needles.size()];
    seed = w5_text_composition_reload_light_fuzz_detail::next_seed(seed);
    const auto &updates_needle = updates_needles[seed % updates_needles.size()];
    seed = w5_text_composition_reload_light_fuzz_detail::next_seed(seed);
    const auto &forbidden = translation_forbidden[seed % translation_forbidden.size()];

    ASSERT_TRUE(normalized_h.contains(header_needle));
    ASSERT_TRUE(normalized_translation.contains(translation_needle));
    ASSERT_TRUE(normalized_updates.contains(updates_needle));
    ASSERT_TRUE(!normalized_translation.contains(forbidden));
  }
}