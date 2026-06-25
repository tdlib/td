// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_style_example_count_option_test_utils.h"

TEST(TextCompositionStyleExampleCountOptionContract, ConfigManagerMapsAicomposeToneExamplesNumToExampleCountOption) {
  auto normalized = td::text_composition_style_example_count_option_test::normalized_config_manager_cpp();

  ASSERT_TRUE(normalized.find(R"({"aicompose_tone_examples_num","text_composition_style_example_count"})") !=
              td::string::npos);
}

TEST(TextCompositionStyleExampleCountOptionContract, OptionManagerDefaultsExampleCountToSeven) {
  auto normalized = td::text_composition_style_example_count_option_test::normalized_option_manager_cpp();

  ASSERT_TRUE(normalized.find(R"(set_default_integer_option("text_composition_style_example_count",7);)") !=
              td::string::npos);
}

TEST(TextCompositionStyleExampleCountOptionContract, AppConfigVersionBumpsForExampleCountOption) {
  auto normalized = td::text_composition_style_example_count_option_test::normalized_config_manager_h();

  ASSERT_TRUE(normalized.find("staticconstexprint32CURRENT_VERSION=132;") != td::string::npos);
}
