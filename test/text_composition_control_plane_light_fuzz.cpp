// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_control_plane_test_utils.h"

namespace {

td::uint32 next_seed(td::uint32 seed) {
  return seed * 1664525u + 1013904223u;
}

}  // namespace

TEST(TextCompositionControlPlaneLightFuzz, DeterministicLiteralMatrixPreservesControlPlaneInvariants) {
  const td::vector<td::string> config_needles = {
      R"({"aicompose_tone_title_length_max","text_composition_style_title_length_max"})",
      R"({"aicompose_tone_prompt_length_max","text_composition_style_prompt_length_max"})"};
  const td::vector<td::string> option_needles = {
      R"(set_default_integer_option("text_composition_style_title_length_max",12);)",
      R"(set_default_integer_option("text_composition_style_prompt_length_max",1024);)",
      R"(set_option_integer("added_text_composition_style_max",get_option_integer("aicompose_tone_saved_limit_premium",20));)",
      R"(set_option_integer("added_text_composition_style_max",get_option_integer("aicompose_tone_saved_limit_default",5));)"};
  const td::vector<td::string> premium_needles = {
      "casetd_api::premiumLimitTypeCustomTextCompositionStyleCount::ID:returnSlice(\"aicompose_tone_saved\");",
      "if(key==\"aicompose_tone_saved\"){returntd_api::make_object<td_api::"
      "premiumLimitTypeCustomTextCompositionStyleCount>();}"};

  td::uint32 seed = 0x5EED1234u;

  for (td::int32 i = 0; i < 4096; i++) {
    seed = next_seed(seed);
    auto normalized_config = td::w5_text_composition_control_plane_test::normalized_config_manager_cpp();
    auto normalized_option = td::w5_text_composition_control_plane_test::normalized_option_manager_cpp();
    auto normalized_premium = td::w5_text_composition_control_plane_test::normalized_premium_cpp();
    auto normalized_header = td::w5_text_composition_control_plane_test::normalized_config_manager_h();
    auto normalized_tl = td::w5_text_composition_control_plane_test::normalized_td_api_tl();

    const auto &config_needle = config_needles[seed % config_needles.size()];
    seed = next_seed(seed);
    const auto &option_needle = option_needles[seed % option_needles.size()];
    seed = next_seed(seed);
    const auto &premium_needle = premium_needles[seed % premium_needles.size()];

    ASSERT_TRUE(normalized_config.contains(config_needle));
    ASSERT_TRUE(normalized_option.contains(option_needle));
    ASSERT_TRUE(normalized_premium.contains(premium_needle));
    ASSERT_TRUE(normalized_header.contains("staticconstexprint32CURRENT_VERSION=132;"));
    ASSERT_TRUE(normalized_tl.contains("premiumLimitTypeCustomTextCompositionStyleCount=PremiumLimitType;"));
  }
}
