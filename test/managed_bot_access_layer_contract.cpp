// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/managed_bot_access_settings_test_utils.h"

#include <cstdlib>

TEST(ManagedBotAccessLayerContract, LayerMustBe225ForManagedBotAccessSettingsConstructors) {
  auto normalized_telegram_api_source = td::managed_bot_access_settings_test::normalized_telegram_api_source();
  auto normalized_version_source = td::managed_bot_access_settings_test::normalized_version_h_source();

  ASSERT_NE(
      td::string::npos,
      normalized_telegram_api_source.find(
          R"(bots.accessSettings#dd1fbf93flags:#restricted:flags.0?trueadd_users:flags.1?Vector<User>=bots.AccessSettings;)"));
  ASSERT_NE(td::string::npos, normalized_telegram_api_source.find(
                                  R"(bots.getAccessSettings#213853a3bot:InputUser=bots.AccessSettings;)"));
  ASSERT_NE(
      td::string::npos,
      normalized_telegram_api_source.find(
          R"(bots.editAccessSettings#31813cd8flags:#restricted:flags.0?truebot:InputUseradd_users:flags.1?Vector<InputUser>=Bool;)"));

  constexpr td::Slice kPrefix = "constexprint32MTPROTO_LAYER=";
  auto pos = normalized_version_source.find(kPrefix.str());
  ASSERT_NE(td::string::npos, pos);

  auto end = normalized_version_source.find(';', pos + kPrefix.size());
  ASSERT_NE(td::string::npos, end);

  auto layer_text = normalized_version_source.substr(pos + kPrefix.size(), end - pos - kPrefix.size());
  auto layer = std::strtol(layer_text.c_str(), nullptr, 10);
  ASSERT_TRUE(layer >= 225);
}
