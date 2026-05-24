// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_style_catalog_sanitizer_test_utils.h"

TEST(TextCompositionStyleCatalogSanitizerAdversarial, DropsStyleNameContainingNulByte) {
  td::string style_name = "tone";
  style_name.push_back('\0');
  style_name += "_custom";

  td::vector<td::string> input = {style_name, "1", "Title", "AbCdEf12", "2", "GoodTitle"};

  auto sanitized = td::w5_text_composition_style_catalog_sanitizer_test::sanitize(std::move(input));

  ASSERT_EQ(static_cast<size_t>(3), sanitized.size());
  ASSERT_EQ("AbCdEf12", sanitized[0]);
  ASSERT_EQ("2", sanitized[1]);
  ASSERT_EQ("GoodTitle", sanitized[2]);
}

TEST(TextCompositionStyleCatalogSanitizerAdversarial, DropsStyleTitleContainingNulByte) {
  td::string title = "Friendly";
  title.push_back('\0');
  title += "Tone";

  td::vector<td::string> input = {"AbCdEf12", "1", title, "tone_custom", "2", "Custom"};

  auto sanitized = td::w5_text_composition_style_catalog_sanitizer_test::sanitize(std::move(input));

  ASSERT_EQ(static_cast<size_t>(3), sanitized.size());
  ASSERT_EQ("tone_custom", sanitized[0]);
  ASSERT_EQ("2", sanitized[1]);
  ASSERT_EQ("Custom", sanitized[2]);
}

TEST(TextCompositionStyleCatalogSanitizerAdversarial, DropsNonUtf8StyleNameAndTitle) {
  td::string invalid_utf8 = "\xC3\x28";

  td::vector<td::string> input = {invalid_utf8, "1",           "Title", "AbCdEf12", "2",
                                  invalid_utf8, "tone_custom", "3",     "Custom"};

  auto sanitized = td::w5_text_composition_style_catalog_sanitizer_test::sanitize(std::move(input));

  ASSERT_EQ(static_cast<size_t>(3), sanitized.size());
  ASSERT_EQ("tone_custom", sanitized[0]);
  ASSERT_EQ("3", sanitized[1]);
  ASSERT_EQ("Custom", sanitized[2]);
}

TEST(TextCompositionStyleCatalogSanitizerAdversarial, DropsOverlyLongStyleName) {
  td::string too_long_name(129, 'a');
  td::vector<td::string> input = {too_long_name, "1", "LongNameTitle", "AbCdEf12", "2", "GoodTitle"};

  auto sanitized = td::w5_text_composition_style_catalog_sanitizer_test::sanitize(std::move(input));

  ASSERT_EQ(static_cast<size_t>(3), sanitized.size());
  ASSERT_EQ("AbCdEf12", sanitized[0]);
  ASSERT_EQ("2", sanitized[1]);
  ASSERT_EQ("GoodTitle", sanitized[2]);
}

TEST(TextCompositionStyleCatalogSanitizerAdversarial, DropsOverlyLongStyleTitle) {
  td::string too_long_title(td::w5_text_composition_style_catalog_sanitizer_test::MAX_STYLE_TITLE_LENGTH + 1, 'T');

  td::vector<td::string> input = {"AbCdEf12", "1", too_long_title, "tone_custom", "2", "Custom"};

  auto sanitized = td::w5_text_composition_style_catalog_sanitizer_test::sanitize(std::move(input));

  ASSERT_EQ(static_cast<size_t>(3), sanitized.size());
  ASSERT_EQ("tone_custom", sanitized[0]);
  ASSERT_EQ("2", sanitized[1]);
  ASSERT_EQ("Custom", sanitized[2]);
}
