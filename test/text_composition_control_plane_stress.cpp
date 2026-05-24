// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_control_plane_test_utils.h"

TEST(TextCompositionControlPlaneStress, RepeatedSourceReadsKeepControlPlaneContractsStable) {
  td::uint64 iterations = 0;
  td::uint64 contract_hits = 0;

  for (td::int32 i = 0; i < 2000; i++) {
    auto normalized_config = td::w5_text_composition_control_plane_test::normalized_config_manager_cpp();
    auto normalized_option = td::w5_text_composition_control_plane_test::normalized_option_manager_cpp();
    auto normalized_premium = td::w5_text_composition_control_plane_test::normalized_premium_cpp();
    auto normalized_header = td::w5_text_composition_control_plane_test::normalized_config_manager_h();

    if (auto normalized_tl = td::w5_text_composition_control_plane_test::normalized_td_api_tl();
        normalized_config.contains(
            R"({"aicompose_tone_title_length_max","text_composition_style_title_length_max"})") &&
        normalized_config.contains(
            R"({"aicompose_tone_prompt_length_max","text_composition_style_prompt_length_max"})") &&
        normalized_option.contains(R"(set_default_integer_option("text_composition_style_title_length_max",12);)") &&
        normalized_option.contains(R"(set_default_integer_option("text_composition_style_prompt_length_max",1024);)") &&
        normalized_option.contains(
            R"(set_option_integer("added_text_composition_style_max",get_option_integer("aicompose_tone_saved_limit_premium",20));)") &&
        normalized_option.contains(
            R"(set_option_integer("added_text_composition_style_max",get_option_integer("aicompose_tone_saved_limit_default",5));)") &&
        normalized_premium.contains("premiumLimitTypeCustomTextCompositionStyleCount") &&
        normalized_header.contains("staticconstexprint32CURRENT_VERSION=125;") &&
        normalized_tl.contains("premiumLimitTypeCustomTextCompositionStyleCount=PremiumLimitType;")) {
      contract_hits++;
    }
    iterations++;
  }

  ASSERT_EQ(iterations, contract_hits);
}
