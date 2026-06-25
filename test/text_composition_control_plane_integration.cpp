// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_control_plane_test_utils.h"

TEST(TextCompositionControlPlaneIntegration, ControlPlaneLiteralsTravelAcrossConfigOptionAndPremiumSeams) {
  auto config_source = td::w5_text_composition_control_plane_test::read_config_manager_cpp();
  auto option_source = td::w5_text_composition_control_plane_test::read_option_manager_cpp();
  auto premium_source = td::w5_text_composition_control_plane_test::read_premium_cpp();

  ASSERT_TRUE(config_source.find("text_composition_style_title_length_max") != td::string::npos);
  ASSERT_TRUE(config_source.find("text_composition_style_prompt_length_max") != td::string::npos);
  ASSERT_TRUE(option_source.find("text_composition_style_title_length_max") != td::string::npos);
  ASSERT_TRUE(option_source.find("text_composition_style_prompt_length_max") != td::string::npos);
  ASSERT_TRUE(option_source.find("added_text_composition_style_max") != td::string::npos);
  ASSERT_TRUE(premium_source.find("aicompose_tone_saved") != td::string::npos);
}

TEST(TextCompositionControlPlaneIntegration, MappingDefaultsAndVersionTravelTogether) {
  auto normalized_config = td::w5_text_composition_control_plane_test::normalized_config_manager_cpp();
  auto normalized_option = td::w5_text_composition_control_plane_test::normalized_option_manager_cpp();
  auto normalized_header = td::w5_text_composition_control_plane_test::normalized_config_manager_h();

  ASSERT_TRUE(
      normalized_config.find(R"({"aicompose_tone_title_length_max","text_composition_style_title_length_max"})") !=
      td::string::npos);
  ASSERT_TRUE(
      normalized_config.find(R"({"aicompose_tone_prompt_length_max","text_composition_style_prompt_length_max"})") !=
      td::string::npos);
  ASSERT_TRUE(normalized_option.find(R"(set_default_integer_option("text_composition_style_title_length_max",12);)") !=
              td::string::npos);
  ASSERT_TRUE(
      normalized_option.find(R"(set_default_integer_option("text_composition_style_prompt_length_max",1024);)") !=
      td::string::npos);
  ASSERT_TRUE(normalized_header.find("staticconstexprint32CURRENT_VERSION=132;") != td::string::npos);
}

TEST(TextCompositionControlPlaneIntegration, PremiumLimitTypeAndSchemaTravelTogether) {
  auto normalized_premium = td::w5_text_composition_control_plane_test::normalized_premium_cpp();
  auto normalized_tl = td::w5_text_composition_control_plane_test::normalized_td_api_tl();

  ASSERT_TRUE(normalized_premium.find("premiumLimitTypeCustomTextCompositionStyleCount") != td::string::npos);
  ASSERT_TRUE(normalized_tl.find("premiumLimitTypeCustomTextCompositionStyleCount=PremiumLimitType;") !=
              td::string::npos);
}
