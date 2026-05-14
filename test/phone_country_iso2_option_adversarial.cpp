// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/phone_country_iso2_option_test_utils.h"

TEST(PhoneCountryIso2OptionAdversarial, ConfigManagerMustNotLeavePhoneCountryIso2InUnhandledJsonValues) {
  auto normalized = td::phone_country_iso2_option_test::normalized_config_manager_cpp();

  ASSERT_TRUE(
      normalized.find(R"(if(key=="phone_country_iso2"){new_values.push_back(std::move(key_value));continue;})") ==
      td::string::npos);
}

TEST(PhoneCountryIso2OptionAdversarial, OptionManagerMustNotOmitPhoneCountryIso2FromInternalAllowlist) {
  auto source = td::phone_country_iso2_option_test::read_option_manager_cpp();

  ASSERT_TRUE(source.find("\"otherwise_relogin_days\",") == td::string::npos ||
              source.find("\"phone_country_iso2\"") > source.find("\"otherwise_relogin_days\","));
}

TEST(PhoneCountryIso2OptionAdversarial, AppConfigVersionMustNotRemainAtLegacyValueAfterAddingPhoneCountryIso2) {
  auto normalized = td::phone_country_iso2_option_test::normalized_config_manager_h();

  ASSERT_TRUE(normalized.find("staticconstexprint32CURRENT_VERSION=119;") == td::string::npos);
}