// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_update_promise_test_utils.h"

namespace w5_text_composition_update_promise_light_fuzz_detail {

td::uint32 next_seed(td::uint32 seed) {
  return seed * 1664525u + 1013904223u;
}

}  // namespace w5_text_composition_update_promise_light_fuzz_detail

TEST(TextCompositionUpdatePromiseLightFuzz, DeterministicLiteralMatrixPreservesPromiseUpdateInvariants) {
  const td::vector<td::string> header_needles = {
      "voidon_update_ai_compose_styles(vector<string>&&ai_compose_styles,Promise<Unit>&&promise);"};
  const td::vector<td::string> cpp_needles = {
      "voidTranslationManager::on_update_ai_compose_styles(vector<string>&&ai_compose_styles,Promise<Unit>&&promise){",
      "on_update_ai_compose_styles(std::move(ai_compose_styles));", "std::move(promise).set_value(Unit());",
      "ai_compose_styles=sanitize_ai_compose_styles(std::move(ai_compose_styles),\"config\");",
      "if(ai_compose_styles==ai_compose_styles_){return;}"};
  const td::vector<td::string> cpp_forbidden = {
      "voidTranslationManager::on_update_ai_compose_styles(vector<string>&&ai_compose_styles,Promise<Unit>&&promise){"
      "return;}",
      "voidTranslationManager::on_update_ai_compose_styles(vector<string>&&ai_compose_styles,Promise<Unit>&&promise){"
      "promise.set_error(",
      "voidTranslationManager::on_update_ai_compose_styles(vector<string>&&ai_compose_styles,Promise<Unit>&&promise){"
      "on_update_ai_compose_styles(std::move(ai_compose_styles));}"};

  td::uint32 seed = 0x51D3B00Fu;

  for (td::int32 i = 0; i < 4096; i++) {
    seed = w5_text_composition_update_promise_light_fuzz_detail::next_seed(seed);
    auto normalized_h = td::w5_text_composition_update_promise_test::normalized_translation_manager_h();
    auto normalized_cpp = td::w5_text_composition_update_promise_test::normalized_translation_manager_cpp();

    const auto &header_needle = header_needles[seed % header_needles.size()];
    seed = w5_text_composition_update_promise_light_fuzz_detail::next_seed(seed);
    const auto &cpp_needle = cpp_needles[seed % cpp_needles.size()];
    seed = w5_text_composition_update_promise_light_fuzz_detail::next_seed(seed);
    const auto &forbidden = cpp_forbidden[seed % cpp_forbidden.size()];

    ASSERT_TRUE(normalized_h.contains(header_needle));
    ASSERT_TRUE(normalized_cpp.contains(cpp_needle));
    ASSERT_TRUE(!normalized_cpp.contains(forbidden));
  }
}
