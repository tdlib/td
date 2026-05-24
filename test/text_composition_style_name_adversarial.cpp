// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/TranslationManager.h"

#include "td/utils/tests.h"

TEST(TextCompositionStyleNameAdversarial, RejectsUnknownStyleWhenCatalogDoesNotContainIt) {
  td::vector<td::string> ai_compose_styles = {"formal", "1", "Formal", "neutral", "2", "Neutral"};

  ASSERT_TRUE(
      td::TranslationManager::validate_text_composition_style_name("not.whitelisted", ai_compose_styles).is_error());
}

TEST(TextCompositionStyleNameAdversarial, RejectsShortSlugLikeNames) {
  ASSERT_TRUE(td::TranslationManager::validate_text_composition_style_name("AbCd123", {}).is_error());
}

TEST(TextCompositionStyleNameAdversarial, RejectsNonBase64UrlCharactersOutsideCatalog) {
  ASSERT_TRUE(td::TranslationManager::validate_text_composition_style_name("AbCd+Ef12", {}).is_error());
}

TEST(TextCompositionStyleNameAdversarial, RejectsEmbeddedNulByte) {
  td::string style_name = "formal";
  style_name.push_back('\0');
  style_name += "suffix";

  ASSERT_TRUE(td::TranslationManager::validate_text_composition_style_name(style_name, {}).is_error());
}

TEST(TextCompositionStyleNameAdversarial, RejectsOverlyLongStyleNames) {
  td::string style_name(129, 'a');

  ASSERT_TRUE(td::TranslationManager::validate_text_composition_style_name(style_name, {}).is_error());
}

TEST(TextCompositionStyleNameAdversarial, RejectsMalformedCatalogForUnknownStyle) {
  td::vector<td::string> malformed_catalog = {"formal", "1", "Formal", "orphan_name_only"};

  ASSERT_TRUE(
      td::TranslationManager::validate_text_composition_style_name("unknown_style", malformed_catalog).is_error());
}
