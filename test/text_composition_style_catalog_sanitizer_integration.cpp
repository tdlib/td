// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/TranslationManager.h"

#include "td/utils/tests.h"

#include "test/text_composition_style_catalog_sanitizer_test_utils.h"

TEST(TextCompositionStyleCatalogSanitizerIntegration,
     SanitizedCatalogFeedsValidationWithoutWhitelistingMalformedNames) {
  td::string too_long_name(129, 'a');

  td::vector<td::string> raw_catalog = {too_long_name, "1", "LongNameTitle", "AbCdEf12", "2", "GoodTitle",
                                        "tone_custom", "3", "Custom"};

  auto sanitized = td::w5_text_composition_style_catalog_sanitizer_test::sanitize(std::move(raw_catalog));

  ASSERT_TRUE(td::TranslationManager::validate_text_composition_style_name("tone_custom", sanitized).is_ok());
  ASSERT_TRUE(td::TranslationManager::validate_text_composition_style_name("AbCdEf12", sanitized).is_ok());
  ASSERT_TRUE(td::TranslationManager::validate_text_composition_style_name(too_long_name, sanitized).is_error());
}

TEST(TextCompositionStyleCatalogSanitizerIntegration, SanitizedCatalogRemainsDeterministicAcrossRepeatedPasses) {
  td::string invalid_utf8 = "\xC3\x28";
  td::vector<td::string> raw_catalog = {"tone_custom", "1",        "Custom", invalid_utf8, "2",
                                        "Broken",      "AbCdEf12", "3",      "SlugTitle"};

  auto once = td::w5_text_composition_style_catalog_sanitizer_test::sanitize(raw_catalog);
  auto twice = td::w5_text_composition_style_catalog_sanitizer_test::sanitize(raw_catalog);

  ASSERT_EQ(once, twice);
  ASSERT_TRUE(td::w5_text_composition_style_catalog_sanitizer_test::is_well_formed_sanitized_catalog(once));
}

TEST(TextCompositionStyleCatalogSanitizerIntegration,
     OversizedTitleStyleIsDroppedFromCatalogButSlugFallbackStillWorks) {
  td::string too_long_title(td::w5_text_composition_style_catalog_sanitizer_test::MAX_STYLE_TITLE_LENGTH + 1, 'T');
  td::vector<td::string> raw_catalog = {"AbCdEf12", "1", too_long_title, "tone_custom", "2", "Custom"};

  auto sanitized = td::w5_text_composition_style_catalog_sanitizer_test::sanitize(std::move(raw_catalog));

  ASSERT_EQ(static_cast<size_t>(3), sanitized.size());
  ASSERT_EQ("tone_custom", sanitized[0]);
  ASSERT_EQ("2", sanitized[1]);
  ASSERT_EQ("Custom", sanitized[2]);

  // Slug fallback remains enabled for forward compatibility and independent links.
  ASSERT_TRUE(td::TranslationManager::validate_text_composition_style_name("AbCdEf12", sanitized).is_ok());
  ASSERT_TRUE(td::TranslationManager::validate_text_composition_style_name("tone_custom", sanitized).is_ok());
}
