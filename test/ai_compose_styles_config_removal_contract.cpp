// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/ai_compose_styles_config_removal_test_utils.h"

TEST(AiComposeStylesConfigRemovalContract, ConfigManagerDropsAiComposeStylesAccumulator) {
  auto normalized = td::ai_compose_styles_config_removal_test::normalized_config_manager_cpp();

  ASSERT_TRUE(normalized.find("vector<string>ai_compose_styles;") == td::string::npos);
}

TEST(AiComposeStylesConfigRemovalContract, ConfigManagerNoLongerConsumesAiComposeStylesAppConfigKey) {
  auto normalized = td::ai_compose_styles_config_removal_test::normalized_config_manager_cpp();

  ASSERT_TRUE(normalized.find(R"(if(key=="ai_compose_styles")") == td::string::npos);
}

TEST(AiComposeStylesConfigRemovalContract, AppConfigVersionBumpsAfterRemovingAiComposeStylesConfigOption) {
  auto normalized = td::ai_compose_styles_config_removal_test::normalized_config_manager_h();

  ASSERT_TRUE(normalized.find("staticconstexprint32CURRENT_VERSION=132;") != td::string::npos);
}
