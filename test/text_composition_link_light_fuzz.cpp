// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include <array>

#include "test/text_composition_link_test_utils.h"

namespace {

struct AddStyleLinkCase {
  td::string url;
  bool expect_style;
  td::string expected_style_name;
};

}  // namespace

TEST(TextCompositionLinkLightFuzz, DeterministicAddStyleMatrixResolvesOrFailsClosed) {
  const std::array<AddStyleLinkCase, 10> cases = {{
      {"tg:addstyle?slug=AbCdEf12", true, "AbCdEf12"},
      {"tg:addstyle?slug=abc%30def1", true, "abc0def1"},
      {"t.me/addstyle/AbCdEf12", true, "AbCdEf12"},
      {"tg:addstyle?slug=AbCdEf12&utm=1", true, "AbCdEf12"},
      {"tg:addstyle?slug=AbCdEf12&slug=QwErTy34", false, ""},
      {"tg:addstyle?slug=AbCdEf12&slug=", false, ""},
      {"t.me/addstyle/AbCdEf12?slug=QwErTy34", false, ""},
      {"t.me/addstyle/AbCdEf12?slug=AbCdEf12", false, ""},
      {"t.me/addstyle/AbCdEf12?slug=AbCdEf12&slug=QwErTy34", false, ""},
      {"tg:addstyle?slug=AbCdEf1", false, ""},
  }};

  for (int iteration = 0; iteration < 10000; iteration++) {
    auto index = static_cast<size_t>(td::Random::fast(0, static_cast<int>(cases.size()) - 1));
    const auto &test_case = cases[index];

    auto style = td::text_composition_link_test::parse_text_composition_style_link(test_case.url);
    if (test_case.expect_style) {
      ASSERT_TRUE(style != nullptr);
      ASSERT_EQ(test_case.expected_style_name, style->style_name_);
    } else {
      ASSERT_TRUE(style == nullptr);
      auto parsed = td::text_composition_link_test::parse_link(test_case.url);
      ASSERT_TRUE(parsed == nullptr || td::text_composition_link_test::is_unknown_deep_link(test_case.url));
    }
  }
}
