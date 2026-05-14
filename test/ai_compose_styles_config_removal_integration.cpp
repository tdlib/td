// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/ai_compose_styles_config_removal_test_utils.h"

TEST(AiComposeStylesConfigRemovalIntegration, ConfigManagerRemovalAndVersionBumpTravelTogether) {
  auto normalized_cpp = td::ai_compose_styles_config_removal_test::normalized_config_manager_cpp();
  auto normalized_h = td::ai_compose_styles_config_removal_test::normalized_config_manager_h();

  ASSERT_TRUE(normalized_cpp.find("vector<string>ai_compose_styles;") == td::string::npos);
  ASSERT_TRUE(normalized_cpp.find(R"(if(key=="ai_compose_styles")") == td::string::npos);
  ASSERT_TRUE(
      normalized_cpp.find(
          R"(send_closure(G()->translation_manager(),&TranslationManager::on_update_ai_compose_styles,std::move(ai_compose_styles));)") ==
      td::string::npos);
  ASSERT_TRUE(normalized_h.find("staticconstexprint32CURRENT_VERSION=121;") != td::string::npos);
}

TEST(AiComposeStylesConfigRemovalIntegration, ConfigManagerStillKeepsNeighborMusicSearchAndPhoneCountryKeys) {
  auto source = td::ai_compose_styles_config_removal_test::read_config_manager_cpp();

  ASSERT_TRUE(source.find("if (key == \"music_search_username\")") != td::string::npos);
  ASSERT_TRUE(source.find("if (key == \"phone_country_iso2\")") != td::string::npos);
}