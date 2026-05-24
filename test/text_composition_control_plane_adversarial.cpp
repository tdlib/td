// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_control_plane_test_utils.h"

TEST(TextCompositionControlPlaneAdversarial, ConfigManagerMustNotLeaveTitleOrPromptKeysUnmapped) {
  auto normalized = td::w5_text_composition_control_plane_test::normalized_config_manager_cpp();

  ASSERT_TRUE(normalized.find(R"({"aicompose_tone_title_length_max",""})") == td::string::npos);
  ASSERT_TRUE(normalized.find(R"({"aicompose_tone_prompt_length_max",""})") == td::string::npos);
}

TEST(TextCompositionControlPlaneAdversarial, OptionManagerMustNotDuplicateTitleAndPromptDefaults) {
  auto normalized = td::w5_text_composition_control_plane_test::normalized_option_manager_cpp();

  ASSERT_EQ(1u, td::w5_text_composition_control_plane_test::count_occurrences(
                    normalized, R"(set_default_integer_option("text_composition_style_title_length_max",12);)"));
  ASSERT_EQ(1u, td::w5_text_composition_control_plane_test::count_occurrences(
                    normalized, R"(set_default_integer_option("text_composition_style_prompt_length_max",1024);)"));
}

TEST(TextCompositionControlPlaneAdversarial, OptionManagerMustKeepDistinctPremiumAndDefaultAddedStyleLimits) {
  auto normalized = td::w5_text_composition_control_plane_test::normalized_option_manager_cpp();

  ASSERT_EQ(
      1u,
      td::w5_text_composition_control_plane_test::count_occurrences(
          normalized,
          R"(set_option_integer("added_text_composition_style_max",get_option_integer("aicompose_tone_saved_limit_premium",20));)"));
  ASSERT_EQ(
      1u,
      td::w5_text_composition_control_plane_test::count_occurrences(
          normalized,
          R"(set_option_integer("added_text_composition_style_max",get_option_integer("aicompose_tone_saved_limit_default",5));)"));
  ASSERT_TRUE(
      normalized.find(
          R"(set_option_integer("added_text_composition_style_max",get_option_integer("aicompose_tone_saved_limit_premium",5));)") ==
      td::string::npos);
  ASSERT_TRUE(
      normalized.find(
          R"(set_option_integer("added_text_composition_style_max",get_option_integer("aicompose_tone_saved_limit_default",20));)") ==
      td::string::npos);
}

TEST(TextCompositionControlPlaneAdversarial,
     OptionManagerMustNotDuplicatePremiumStorySuggestedReactionAreaAssignment) {
  auto normalized = td::w5_text_composition_control_plane_test::normalized_option_manager_cpp();

  ASSERT_EQ(
      1u,
      td::w5_text_composition_control_plane_test::count_occurrences(
          normalized,
          R"(set_option_integer("story_suggested_reaction_area_count_max",get_option_integer("stories_suggested_reactions_limit_premium",5));)"));
}

TEST(TextCompositionControlPlaneAdversarial, PremiumLimitMappingMustBeBidirectionalAndExplicit) {
  auto normalized = td::w5_text_composition_control_plane_test::normalized_premium_cpp();

  ASSERT_TRUE(
      normalized.find(
          "casetd_api::premiumLimitTypeCustomTextCompositionStyleCount::ID:returnSlice(\"aicompose_tone_saved\");") !=
      td::string::npos);
  ASSERT_TRUE(normalized.find("if(key==\"aicompose_tone_saved\"){returntd_api::make_object<td_api::"
                              "premiumLimitTypeCustomTextCompositionStyleCount>();}") != td::string::npos);
}

TEST(TextCompositionControlPlaneAdversarial, AppConfigVersionMustNotStayAtLegacyControlPlaneValues) {
  auto normalized = td::w5_text_composition_control_plane_test::normalized_config_manager_h();

  ASSERT_TRUE(normalized.find("staticconstexprint32CURRENT_VERSION=122;") == td::string::npos);
  ASSERT_TRUE(normalized.find("staticconstexprint32CURRENT_VERSION=123;") == td::string::npos);
  ASSERT_TRUE(normalized.find("staticconstexprint32CURRENT_VERSION=124;") == td::string::npos);
}
