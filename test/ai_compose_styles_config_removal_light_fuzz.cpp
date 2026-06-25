// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include "test/ai_compose_styles_config_removal_test_utils.h"

#include <array>

namespace {

struct SnippetCase {
  bool use_header;
  td::string snippet;
  bool expected_present;
};

}  // namespace

TEST(AiComposeStylesConfigRemovalLightFuzz, DeterministicLiteralMatrixPreservesRemovalInvariants) {
  const auto normalized_cpp = td::ai_compose_styles_config_removal_test::normalized_config_manager_cpp();
  const auto normalized_h = td::ai_compose_styles_config_removal_test::normalized_config_manager_h();

  ASSERT_TRUE(normalized_h.find("staticconstexprint32CURRENT_VERSION=132;") != td::string::npos);

  const std::array<SnippetCase, 7> cases = {{
      {false, "vector<string>ai_compose_styles;", false},
      {false, R"(if(key=="ai_compose_styles")", false},
      {false,
       R"(send_closure(G()->translation_manager(),&TranslationManager::on_update_ai_compose_styles,std::move(ai_compose_styles));)",
       false},
      {false, R"({"music_search_username","audio_search_bot_username"})", true},
      {false, R"(if(key=="phone_country_iso2")", true},
      {true, "staticconstexprint32CURRENT_VERSION=132;", true},
      {true, "staticconstexprint32CURRENT_VERSION=120;", false},
  }};

  for (int iteration = 0; iteration < 10000; iteration++) {
    const auto &test_case = cases[static_cast<size_t>(td::Random::fast(0, static_cast<int>(cases.size()) - 1))];
    const auto &source = test_case.use_header ? normalized_h : normalized_cpp;
    ASSERT_EQ(test_case.expected_present, source.find(test_case.snippet) != td::string::npos);
  }
}
