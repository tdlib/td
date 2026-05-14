// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_style_example_count_option_test_utils.h"

TEST(TextCompositionStyleExampleCountOptionIntegration,
     ConfigManagerAndOptionManagerShareExampleCountControlPlaneLiteral) {
  auto config_source = td::text_composition_style_example_count_option_test::read_config_manager_cpp();
  auto option_source = td::text_composition_style_example_count_option_test::read_option_manager_cpp();

  ASSERT_TRUE(config_source.find("text_composition_style_example_count") != td::string::npos);
  ASSERT_TRUE(option_source.find("text_composition_style_example_count") != td::string::npos);
}

TEST(TextCompositionStyleExampleCountOptionIntegration, MappingDefaultAndVersionTravelTogether) {
  auto normalized_config = td::text_composition_style_example_count_option_test::normalized_config_manager_cpp();
  auto normalized_option = td::text_composition_style_example_count_option_test::normalized_option_manager_cpp();
  auto normalized_header = td::text_composition_style_example_count_option_test::normalized_config_manager_h();

  ASSERT_TRUE(normalized_config.find(R"({"aicompose_tone_examples_num","text_composition_style_example_count"})") !=
              td::string::npos);
  ASSERT_TRUE(normalized_option.find(R"(set_default_integer_option("text_composition_style_example_count",7);)") !=
              td::string::npos);
  ASSERT_TRUE(normalized_header.find("staticconstexprint32CURRENT_VERSION=122;") != td::string::npos);
}