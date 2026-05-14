// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include "test/phone_country_iso2_option_test_utils.h"

#include <array>

namespace {

struct SnippetCase {
  bool use_config_source;
  td::string snippet;
  bool expected_present;
};

}  // namespace

TEST(PhoneCountryIso2OptionLightFuzz, DeterministicLiteralMatrixPreservesConfigOptionInvariants) {
  const auto normalized_config = td::phone_country_iso2_option_test::normalized_config_manager_cpp();
  const auto normalized_option = td::phone_country_iso2_option_test::normalized_option_manager_cpp();

  const std::array<SnippetCase, 5> cases = {{
      {true,
       R"(if(key=="phone_country_iso2"){G()->set_option_string("phone_country_iso2",get_json_value_string(std::move(key_value->value_),key));continue;})",
       true},
      {true, R"(if(key=="phone_country_iso2"){new_values.push_back(std::move(key_value));continue;})", false},
      {false, "\"phone_country_iso2\"", true},
      {false, "\"otherwise_relogin_days\"", true},
      {false, "\"phone_country_iso3\"", false},
  }};

  for (int iteration = 0; iteration < 10000; iteration++) {
    const auto &test_case = cases[static_cast<size_t>(td::Random::fast(0, static_cast<int>(cases.size()) - 1))];
    const auto &source = test_case.use_config_source ? normalized_config : normalized_option;

    ASSERT_EQ(test_case.expected_present, source.find(test_case.snippet) != td::string::npos);
  }
}