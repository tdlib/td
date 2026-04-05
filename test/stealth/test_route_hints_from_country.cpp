//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/stealth/Interfaces.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::route_hints_from_country_code;

TEST(RouteHintsFromCountry, EmptyCountryCodeIsUnknown) {
  auto hints = route_hints_from_country_code("");
  ASSERT_FALSE(hints.is_known);
  ASSERT_FALSE(hints.is_ru);
}

TEST(RouteHintsFromCountry, InvalidCountryCodeIsUnknown) {
  {
    auto hints = route_hints_from_country_code("RUS");
    ASSERT_FALSE(hints.is_known);
    ASSERT_FALSE(hints.is_ru);
  }
  {
    auto hints = route_hints_from_country_code("R1");
    ASSERT_FALSE(hints.is_known);
    ASSERT_FALSE(hints.is_ru);
  }
}

TEST(RouteHintsFromCountry, RuCountryCodeEnablesRuLane) {
  auto hints = route_hints_from_country_code("ru");
  ASSERT_TRUE(hints.is_known);
  ASSERT_TRUE(hints.is_ru);
}

TEST(RouteHintsFromCountry, NonRuCountryCodeEnablesKnownNonRuLane) {
  auto hints = route_hints_from_country_code("US");
  ASSERT_TRUE(hints.is_known);
  ASSERT_FALSE(hints.is_ru);
}

}  // namespace
