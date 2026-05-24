// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include "test/text_composition_style_example_count_option_test_utils.h"

#include <array>

namespace {

struct SnippetCase {
  int source_kind;
  td::string snippet;
  bool expected_present;
};

}  // namespace

TEST(TextCompositionStyleExampleCountOptionLightFuzz, DeterministicLiteralMatrixPreservesOptionInvariants) {
  const auto normalized_config = td::text_composition_style_example_count_option_test::normalized_config_manager_cpp();
  const auto normalized_option = td::text_composition_style_example_count_option_test::normalized_option_manager_cpp();
  const auto normalized_header = td::text_composition_style_example_count_option_test::normalized_config_manager_h();

  const std::array<SnippetCase, 7> cases = {{
      {0, R"({"aicompose_tone_examples_num","text_composition_style_example_count"})", true},
      {0, R"({"aicompose_tone_examples_num",""})", false},
      {1, R"(set_default_integer_option("text_composition_style_example_count",7);)", true},
      {1, R"(set_default_integer_option("text_composition_style_example_count",8);)", false},
      {1, R"(set_default_integer_option("owned_bot_count_max",20);)", true},
      {2, "staticconstexprint32CURRENT_VERSION=125;", true},
      {2, "staticconstexprint32CURRENT_VERSION=121;", false},
  }};

  for (int iteration = 0; iteration < 10000; iteration++) {
    const auto &test_case = cases[static_cast<size_t>(td::Random::fast(0, static_cast<int>(cases.size()) - 1))];

    const td::string *source = &normalized_header;
    if (test_case.source_kind == 0) {
      source = &normalized_config;
    } else if (test_case.source_kind == 1) {
      source = &normalized_option;
    }

    ASSERT_EQ(test_case.expected_present, source->contains(test_case.snippet));
  }
}