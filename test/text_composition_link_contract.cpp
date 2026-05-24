// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_link_test_utils.h"

TEST(TextCompositionLinkContract, CanonicalAddStyleLinksRemainSupportedAcrossSchemes) {
  ASSERT_TRUE(td::text_composition_link_test::is_text_composition_style_link("t.me/addstyle/AbCdEf12", "AbCdEf12"));
  ASSERT_TRUE(td::text_composition_link_test::is_text_composition_style_link("tg:addstyle?slug=AbCdEf12", "AbCdEf12"));
}

TEST(TextCompositionLinkContract, LinkBuilderProducesRoundTrippableAddStyleLinks) {
  for (auto is_internal : {true, false}) {
    auto built = td::text_composition_link_test::build_text_composition_style_link("AbCdEf12", is_internal);
    ASSERT_TRUE(built.is_ok());
    ASSERT_TRUE(td::text_composition_link_test::is_text_composition_style_link(built.ok(), "AbCdEf12"));
  }
}

TEST(TextCompositionLinkContract, PercentEncodedBase64UrlSlugRemainsSupportedForQueryForm) {
  ASSERT_TRUE(
      td::text_composition_link_test::is_text_composition_style_link("tg:addstyle?slug=abc%30def1", "abc0def1"));
}

TEST(TextCompositionLinkContract, PathFormMustRejectConflictingSlugQueryParameter) {
  ASSERT_TRUE(!td::text_composition_link_test::is_text_composition_style_link("t.me/addstyle/AbCdEf12?slug=QwErTy34",
                                                                              "AbCdEf12"));
}
