// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_style_example_count_option_test_utils.h"

TEST(TextCompositionStyleExampleCountOptionStress, RepeatedSourceReadsKeepExampleCountOptionStable) {
  constexpr int kIterations = 3000;

  for (int iteration = 0; iteration < kIterations; iteration++) {
    auto normalized_config = td::text_composition_style_example_count_option_test::normalized_config_manager_cpp();
    auto normalized_option = td::text_composition_style_example_count_option_test::normalized_option_manager_cpp();
    auto normalized_header = td::text_composition_style_example_count_option_test::normalized_config_manager_h();

    ASSERT_EQ(1u, td::text_composition_style_example_count_option_test::count_occurrences(
                      normalized_config, R"({"aicompose_tone_examples_num","text_composition_style_example_count"})"));
    ASSERT_EQ(1u, td::text_composition_style_example_count_option_test::count_occurrences(
                      normalized_option, R"(set_default_integer_option("text_composition_style_example_count",7);)"));
    ASSERT_EQ(1u, td::text_composition_style_example_count_option_test::count_occurrences(
                      normalized_header, "staticconstexprint32CURRENT_VERSION=125;"));
  }
}