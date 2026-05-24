// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/phone_country_iso2_option_test_utils.h"

TEST(PhoneCountryIso2OptionStress, RepeatedSourceReadsKeepPhoneCountryIso2BranchAndVersionStable) {
  constexpr int kIterations = 3000;

  for (int iteration = 0; iteration < kIterations; iteration++) {
    auto normalized_config = td::phone_country_iso2_option_test::normalized_config_manager_cpp();
    auto normalized_option = td::phone_country_iso2_option_test::normalized_option_manager_cpp();
    auto normalized_header = td::phone_country_iso2_option_test::normalized_config_manager_h();

    ASSERT_TRUE(
        normalized_config.find(
            R"(if(key=="phone_country_iso2"){G()->set_option_string("phone_country_iso2",get_json_value_string(std::move(key_value->value_),key));continue;})") !=
        td::string::npos);
    ASSERT_TRUE(normalized_option.find("\"phone_country_iso2\"") != td::string::npos);
    ASSERT_EQ(1u, td::phone_country_iso2_option_test::count_occurrences(normalized_header,
                                                                        "staticconstexprint32CURRENT_VERSION="));
    ASSERT_TRUE(td::phone_country_iso2_option_test::extract_current_version(normalized_header) >= 120);
  }
}