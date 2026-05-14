// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/ai_compose_styles_config_removal_test_utils.h"

TEST(AiComposeStylesConfigRemovalAdversarial, ConfigManagerMustNotForwardAiComposeStylesToTranslationManager) {
  auto normalized = td::ai_compose_styles_config_removal_test::normalized_config_manager_cpp();

  ASSERT_TRUE(
      normalized.find(
          R"(send_closure(G()->translation_manager(),&TranslationManager::on_update_ai_compose_styles,std::move(ai_compose_styles));)") ==
      td::string::npos);
}

TEST(AiComposeStylesConfigRemovalAdversarial, ConfigManagerMustNotKeepLegacyAiComposeStylesParsingLoop) {
  auto normalized = td::ai_compose_styles_config_removal_test::normalized_config_manager_cpp();

  ASSERT_TRUE(normalized.find("Receiveunexpectedai_compose_styles") == td::string::npos);
  ASSERT_TRUE(normalized.find("Receiveinvalidstyle") == td::string::npos);
}

TEST(AiComposeStylesConfigRemovalAdversarial, AppConfigVersionMustNotStayAt120AfterRemoval) {
  auto normalized = td::ai_compose_styles_config_removal_test::normalized_config_manager_h();

  ASSERT_TRUE(normalized.find("staticconstexprint32CURRENT_VERSION=120;") == td::string::npos);
}