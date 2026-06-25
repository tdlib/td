// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_control_plane_test_utils.h"

TEST(TextCompositionControlPlaneContract, ConfigManagerMapsTitleAndPromptLengthAppConfigKeys) {
  auto normalized = td::w5_text_composition_control_plane_test::normalized_config_manager_cpp();

  ASSERT_TRUE(normalized.find(R"({"aicompose_tone_title_length_max","text_composition_style_title_length_max"})") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find(R"({"aicompose_tone_prompt_length_max","text_composition_style_prompt_length_max"})") !=
              td::string::npos);
}

TEST(TextCompositionControlPlaneContract, OptionManagerSeedsTitleAndPromptLengthDefaults) {
  auto normalized = td::w5_text_composition_control_plane_test::normalized_option_manager_cpp();

  ASSERT_TRUE(normalized.find(R"(set_default_integer_option("text_composition_style_title_length_max",12);)") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find(R"(set_default_integer_option("text_composition_style_prompt_length_max",1024);)") !=
              td::string::npos);
}

TEST(TextCompositionControlPlaneContract, OptionManagerPremiumPathExportsAddedTextCompositionStyleLimit) {
  auto normalized = td::w5_text_composition_control_plane_test::normalized_option_manager_cpp();

  ASSERT_TRUE(
      normalized.find(
          R"(set_option_integer("added_text_composition_style_max",get_option_integer("aicompose_tone_saved_limit_premium",20));)") !=
      td::string::npos);
  ASSERT_TRUE(
      normalized.find(
          R"(set_option_integer("added_text_composition_style_max",get_option_integer("aicompose_tone_saved_limit_default",5));)") !=
      td::string::npos);
}

TEST(TextCompositionControlPlaneContract, PremiumLimitSurfaceDeclaresCustomTextCompositionStyleKeyAndType) {
  auto normalized = td::w5_text_composition_control_plane_test::normalized_premium_cpp();

  ASSERT_TRUE(normalized.find(R"("aicompose_tone_saved")") != td::string::npos);
  ASSERT_TRUE(normalized.find("premiumLimitTypeCustomTextCompositionStyleCount") != td::string::npos);
  ASSERT_TRUE(normalized.find("returnSlice(\"aicompose_tone_saved\");") != td::string::npos);
}

TEST(TextCompositionControlPlaneContract, TdApiDeclaresCustomTextCompositionStylePremiumLimitType) {
  auto normalized = td::w5_text_composition_control_plane_test::normalized_td_api_tl();

  ASSERT_TRUE(normalized.find("premiumLimitTypeCustomTextCompositionStyleCount=PremiumLimitType;") != td::string::npos);
}

TEST(TextCompositionControlPlaneContract, AppConfigVersionBumpsForControlPlaneExtensions) {
  auto normalized = td::w5_text_composition_control_plane_test::normalized_config_manager_h();

  ASSERT_TRUE(normalized.find("staticconstexprint32CURRENT_VERSION=132;") != td::string::npos);
}
