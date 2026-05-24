// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_link_test_utils.h"

TEST(TextCompositionLinkStress, DuplicateSlugPollutionRemainsFailClosedUnderSustainedParsing) {
  constexpr int kIterations = 50000;

  for (int i = 0; i < kIterations; i++) {
    ASSERT_TRUE(td::text_composition_link_test::is_unknown_deep_link("tg:addstyle?slug=AbCdEf12&slug=QwErTy34"));

    ASSERT_TRUE(!td::text_composition_link_test::is_text_composition_style_link("t.me/addstyle/AbCdEf12?slug=QwErTy34",
                                                                                "AbCdEf12"));

    ASSERT_TRUE(
        td::text_composition_link_test::is_text_composition_style_link("tg:addstyle?slug=AbCdEf12", "AbCdEf12"));
  }
}
