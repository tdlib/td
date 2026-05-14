// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/phone_country_iso2_option_test_utils.h"

TEST(PhoneCountryIso2OptionIntegration, ConfigManagerAndOptionManagerSharePhoneCountryIso2ControlPlaneLiteral) {
  auto config_source = td::phone_country_iso2_option_test::read_config_manager_cpp();
  auto option_source = td::phone_country_iso2_option_test::read_option_manager_cpp();

  ASSERT_TRUE(config_source.find("phone_country_iso2") != td::string::npos);
  ASSERT_TRUE(option_source.find("phone_country_iso2") != td::string::npos);
}

TEST(PhoneCountryIso2OptionIntegration, VersionBumpAndConfigBranchTravelTogether) {
  auto normalized_config = td::phone_country_iso2_option_test::normalized_config_manager_cpp();
  auto normalized_header = td::phone_country_iso2_option_test::normalized_config_manager_h();

  ASSERT_TRUE(
      normalized_config.find(
          R"(if(key=="phone_country_iso2"){G()->set_option_string("phone_country_iso2",get_json_value_string(std::move(key_value->value_),key));continue;})") !=
      td::string::npos);
  ASSERT_TRUE(normalized_header.find("staticconstexprint32CURRENT_VERSION=120;") != td::string::npos);
}