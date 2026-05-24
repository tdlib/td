// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/phone_country_iso2_option_test_utils.h"

TEST(PhoneCountryIso2OptionContract, ConfigManagerConsumesPhoneCountryIso2IntoStringOption) {
  auto normalized = td::phone_country_iso2_option_test::normalized_config_manager_cpp();

  ASSERT_TRUE(
      normalized.find(
          R"(if(key=="phone_country_iso2"){G()->set_option_string("phone_country_iso2",get_json_value_string(std::move(key_value->value_),key));continue;})") !=
      td::string::npos);
}

TEST(PhoneCountryIso2OptionContract, OptionManagerTreatsPhoneCountryIso2AsInternalOption) {
  auto source = td::phone_country_iso2_option_test::read_option_manager_cpp();

  ASSERT_TRUE(source.find("\"phone_country_iso2\"") != td::string::npos);
}

TEST(PhoneCountryIso2OptionContract, AppConfigVersionBumpsForPhoneCountryIso2Schema) {
  auto normalized = td::phone_country_iso2_option_test::normalized_config_manager_h();

  ASSERT_TRUE(normalized.find("staticconstexprint32CURRENT_VERSION=") != td::string::npos);
  ASSERT_TRUE(td::phone_country_iso2_option_test::extract_current_version(normalized) >= 120);
}