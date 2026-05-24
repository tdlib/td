// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_link_test_utils.h"

TEST(TextCompositionLinkAdversarial, DuplicateSlugQueryParametersMustFailClosed) {
  const td::string url = "tg:addstyle?slug=AbCdEf12&slug=QwErTy34";

  auto unknown = td::text_composition_link_test::parse_unknown_deep_link(url);
  ASSERT_TRUE(unknown != nullptr);
  ASSERT_EQ("tg://addstyle?slug=AbCdEf12&slug=QwErTy34", unknown->link_);
}

TEST(TextCompositionLinkAdversarial, DuplicateSlugQueryParametersWithInvalidTailMustFailClosed) {
  ASSERT_TRUE(td::text_composition_link_test::is_unknown_deep_link("tg:addstyle?slug=AbCdEf12&slug=AbCdEf1"));
}

TEST(TextCompositionLinkAdversarial, DuplicateSlugQueryParametersWithEmptyTailMustFailClosed) {
  ASSERT_TRUE(td::text_composition_link_test::is_unknown_deep_link("tg:addstyle?slug=AbCdEf12&slug="));
}

TEST(TextCompositionLinkAdversarial, PathSlugWithConflictingSlugQueryMustFailClosed) {
  ASSERT_TRUE(!td::text_composition_link_test::is_text_composition_style_link("t.me/addstyle/AbCdEf12?slug=QwErTy34",
                                                                              "AbCdEf12"));
}

TEST(TextCompositionLinkAdversarial, PathSlugWithDuplicateSlugQueryMustFailClosed) {
  ASSERT_TRUE(!td::text_composition_link_test::is_text_composition_style_link(
      "t.me/addstyle/AbCdEf12?slug=AbCdEf12&slug=QwErTy34", "AbCdEf12"));
}
