// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
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
