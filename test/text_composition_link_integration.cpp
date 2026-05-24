// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_link_test_utils.h"

TEST(TextCompositionLinkIntegration, SingleSlugWithAdditionalTrackingParameterRemainsDeterministic) {
  ASSERT_TRUE(
      td::text_composition_link_test::is_text_composition_style_link("tg:addstyle?slug=AbCdEf12&utm=1", "AbCdEf12"));
}

TEST(TextCompositionLinkIntegration, DuplicateSlugNeverResolvesAsTextCompositionStyle) {
  ASSERT_TRUE(!td::text_composition_link_test::is_text_composition_style_link("tg:addstyle?slug=AbCdEf12&slug=QwErTy34",
                                                                              "AbCdEf12"));
}

TEST(TextCompositionLinkIntegration, ValidPathAndInvalidDuplicatedQueryFormsDoNotAliasEachOther) {
  ASSERT_TRUE(td::text_composition_link_test::is_text_composition_style_link("t.me/addstyle/AbCdEf12", "AbCdEf12"));
  ASSERT_TRUE(td::text_composition_link_test::is_unknown_deep_link("tg:addstyle?slug=AbCdEf12&slug=QwErTy34"));
}

TEST(TextCompositionLinkIntegration, PathFormWithConflictingSlugQueryDoesNotAliasCleanPathForm) {
  ASSERT_TRUE(td::text_composition_link_test::is_text_composition_style_link("t.me/addstyle/AbCdEf12", "AbCdEf12"));
  ASSERT_TRUE(!td::text_composition_link_test::is_text_composition_style_link("t.me/addstyle/AbCdEf12?slug=QwErTy34",
                                                                              "AbCdEf12"));
}
