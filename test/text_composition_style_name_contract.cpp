// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/TranslationManager.h"

#include "td/utils/tests.h"

TEST(TextCompositionStyleNameContract, AllowsEmptyStyleName) {
  ASSERT_TRUE(td::TranslationManager::validate_text_composition_style_name(td::Slice(), {}).is_ok());
}

TEST(TextCompositionStyleNameContract, AllowsLegacyBuiltInStylesWithoutCatalog) {
  ASSERT_TRUE(td::TranslationManager::validate_text_composition_style_name("formal", {}).is_ok());
  ASSERT_TRUE(td::TranslationManager::validate_text_composition_style_name("neutral", {}).is_ok());
  ASSERT_TRUE(td::TranslationManager::validate_text_composition_style_name("casual", {}).is_ok());
}

TEST(TextCompositionStyleNameContract, AllowsCatalogStyleNames) {
  td::vector<td::string> ai_compose_styles = {"formal", "1", "Formal", "tone_custom", "2", "Custom"};

  ASSERT_TRUE(td::TranslationManager::validate_text_composition_style_name("tone_custom", ai_compose_styles).is_ok());
}

TEST(TextCompositionStyleNameContract, AllowsBase64UrlSlugFallbackWithMinimumLength) {
  ASSERT_TRUE(td::TranslationManager::validate_text_composition_style_name("AbCdEf12", {}).is_ok());
}
