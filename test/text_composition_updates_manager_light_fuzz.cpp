// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_updates_manager_test_utils.h"

namespace w5_text_composition_updates_manager_light_fuzz_detail {

td::uint32 next_seed(td::uint32 seed) {
  return seed * 1664525u + 1013904223u;
}

}  // namespace w5_text_composition_updates_manager_light_fuzz_detail

TEST(TextCompositionUpdatesManagerLightFuzz, DeterministicLiteralMatrixPreservesBridgeInvariants) {
  const td::vector<td::string> telegram_api_tl_needles = {
      td::w5_text_composition_updates_manager_test::ai_compose_tones_update_constructor()};
  const td::vector<td::string> header_needles = {
      td::w5_text_composition_updates_manager_test::ai_compose_styles_bridge_declaration(),
      td::w5_text_composition_updates_manager_test::ai_compose_tones_update_declaration()};
  const td::vector<td::string> cpp_needles = {
      td::w5_text_composition_updates_manager_test::ai_compose_styles_bridge_signature(),
      td::w5_text_composition_updates_manager_test::ai_compose_styles_bridge_bot_guard(),
      td::w5_text_composition_updates_manager_test::ai_compose_styles_bridge_dispatch_call(),
      td::w5_text_composition_updates_manager_test::ai_compose_tones_update_signature(),
      td::w5_text_composition_updates_manager_test::ai_compose_tones_update_reload_call(),
      td::w5_text_composition_updates_manager_test::ai_compose_tones_update_bot_guard(),
      td::w5_text_composition_updates_manager_test::ai_compose_tones_update_promise_completion()};
  const td::vector<td::string> cpp_forbidden = {
      td::string("voidUpdatesManager::on_update_ai_compose_styles(vector<string>&&ai_compose_styles,Promise<Unit>&&"
                 "promise){promise.set_value(Unit());}"),
      td::string(
          "voidUpdatesManager::on_update_ai_compose_styles(vector<string>&&ai_compose_styles,Promise<Unit>&&promise){"
          "td_->translation_manager_->on_update_ai_compose_styles(std::move(ai_compose_styles),std::move(promise));}"),
      td::string("send_closure(td_->translation_manager_actor_,static_cast<void(TranslationManager::*)(vector<string>&&"
                 ",Promise<Unit>&&)>(&TranslationManager::on_update_ai_compose_styles),std::move(ai_compose_styles),"
                 "Promise<Unit>());"),
      td::string("send_closure(td_->translation_manager_actor_,static_cast<void(TranslationManager::*)(vector<string>&&"
                 ",Promise<Unit>&&)>(&TranslationManager::on_update_ai_compose_styles),std::move(ai_compose_styles),"
                 "std::move(promise));std::move(promise).set_value(Unit());"),
      td::string("voidUpdatesManager::on_update(tl_object_ptr<telegram_api::updateAiComposeTones>update,Promise<Unit>&&"
                 "promise){promise.set_value(Unit());}"),
      td::string("voidUpdatesManager::on_update(tl_object_ptr<telegram_api::updateAiComposeTones>update,Promise<Unit>&&"
                 "promise){td_->translation_manager_->reload_ai_compose_tones(Auto());}"),
      td::string("voidUpdatesManager::on_update(tl_object_ptr<telegram_api::updateAiComposeTones>update,Promise<Unit>&&"
                 "promise){td_->translation_manager_->reload_ai_compose_tones(Auto());std::move(promise).set_value("
                 "Unit());}"),
      td::string("voidUpdatesManager::on_update(tl_object_ptr<telegram_api::updateAiComposeTones>update,Promise<Unit>&&"
                 "promise){send_closure(td_->translation_manager_actor_,static_cast<void(TranslationManager::*)(vector<"
                 "string>&&,Promise<Unit>&&)>(&TranslationManager::on_update_ai_compose_styles),vector<string>(),std::"
                 "move(promise));}")};

  td::uint32 seed = 0x7A6ECAFEu;

  for (td::int32 i = 0; i < 4096; i++) {
    seed = w5_text_composition_updates_manager_light_fuzz_detail::next_seed(seed);
    auto normalized_tl = td::w5_text_composition_updates_manager_test::normalized_telegram_api_tl();
    auto normalized_h = td::w5_text_composition_updates_manager_test::normalized_updates_manager_h();
    auto normalized_cpp = td::w5_text_composition_updates_manager_test::normalized_updates_manager_cpp();

    const auto &tl_needle = telegram_api_tl_needles[seed % telegram_api_tl_needles.size()];
    seed = w5_text_composition_updates_manager_light_fuzz_detail::next_seed(seed);
    const auto &header_needle = header_needles[seed % header_needles.size()];
    seed = w5_text_composition_updates_manager_light_fuzz_detail::next_seed(seed);
    const auto &cpp_needle = cpp_needles[seed % cpp_needles.size()];
    seed = w5_text_composition_updates_manager_light_fuzz_detail::next_seed(seed);
    const auto &forbidden = cpp_forbidden[seed % cpp_forbidden.size()];

    ASSERT_TRUE(normalized_tl.contains(tl_needle));
    ASSERT_TRUE(normalized_h.contains(header_needle));
    ASSERT_TRUE(normalized_cpp.contains(cpp_needle));
    ASSERT_TRUE(!normalized_cpp.contains(forbidden));
  }
}
