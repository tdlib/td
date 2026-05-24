// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_style_catalog_sanitizer_test_utils.h"

TEST(TextCompositionStyleCatalogSanitizerContract, KeepsWellFormedTriples) {
  td::vector<td::string> input = {"formal", "1", "Formal", "tone_custom", "2", "Custom", "AbCdEf12", "3", "SlugTitle"};

  auto sanitized = td::w5_text_composition_style_catalog_sanitizer_test::sanitize(input);

  ASSERT_EQ(input, sanitized);
  ASSERT_TRUE(td::w5_text_composition_style_catalog_sanitizer_test::is_well_formed_sanitized_catalog(sanitized));
}

TEST(TextCompositionStyleCatalogSanitizerContract, KeepsTitleAtMaxLengthBoundary) {
  td::string max_title(td::w5_text_composition_style_catalog_sanitizer_test::MAX_STYLE_TITLE_LENGTH, 'T');
  td::vector<td::string> input = {"AbCdEf12", "1", max_title};

  auto sanitized = td::w5_text_composition_style_catalog_sanitizer_test::sanitize(input);

  ASSERT_EQ(input, sanitized);
  ASSERT_TRUE(td::w5_text_composition_style_catalog_sanitizer_test::is_well_formed_sanitized_catalog(sanitized));
}

TEST(TextCompositionStyleCatalogSanitizerContract, RejectsMalformedFieldCount) {
  td::vector<td::string> input = {"formal", "1", "Formal", "orphan"};

  auto sanitized = td::w5_text_composition_style_catalog_sanitizer_test::sanitize(std::move(input));

  ASSERT_TRUE(sanitized.empty());
}

TEST(TextCompositionStyleCatalogSanitizerContract, RejectsTriplesWithNonPositiveStyleId) {
  td::vector<td::string> input = {"formal", "0", "Formal", "tone_custom", "-1", "Custom", "AbCdEf12", "1", "SlugTitle"};

  auto sanitized = td::w5_text_composition_style_catalog_sanitizer_test::sanitize(std::move(input));

  ASSERT_EQ(static_cast<size_t>(3), sanitized.size());
  ASSERT_EQ("AbCdEf12", sanitized[0]);
  ASSERT_EQ("1", sanitized[1]);
  ASSERT_EQ("SlugTitle", sanitized[2]);
}
