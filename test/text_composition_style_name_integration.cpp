// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_style_name_test_utils.h"

TEST(TextCompositionStyleNameIntegration, ComposeFlowMustValidateStyleNameBeforeRpcDispatch) {
  auto normalized = td::w5_text_composition_style_name_test::normalized_translation_manager_cpp();

  auto validate_pos = normalized.find("validate_text_composition_style_name(tone,ai_compose_styles_)");
  auto send_pos = normalized.find("create_handler<ComposeMessageWithAiQuery>");

  ASSERT_TRUE(validate_pos != td::string::npos);
  ASSERT_TRUE(send_pos != td::string::npos);
  ASSERT_TRUE(validate_pos < send_pos);
}

TEST(TextCompositionStyleNameIntegration, SlugValidationMustRequireBase64UrlAndMinimumLength) {
  auto normalized = td::w5_text_composition_style_name_test::normalized_translation_manager_cpp();

  ASSERT_TRUE(normalized.find("slug.size()>=MIN_TEXT_COMPOSITION_STYLE_SLUG_LENGTH") != td::string::npos);
  ASSERT_TRUE(normalized.find("is_base64url_characters(slug)") != td::string::npos);
  ASSERT_TRUE(normalized.find("is_valid_text_composition_style_slug(style_name)") != td::string::npos);
}

TEST(TextCompositionStyleNameIntegration, ComposeFlowMustNotDuplicateStyleNameValidation) {
  auto normalized = td::w5_text_composition_style_name_test::normalized_translation_manager_cpp();

  ASSERT_EQ(1u, td::w5_text_composition_style_name_test::count_occurrences(
                    normalized, "validate_text_composition_style_name(tone,ai_compose_styles_)"));
}

TEST(TextCompositionStyleNameIntegration, LinkParserMustReuseSharedSlugValidationContract) {
  auto normalized = td::w5_text_composition_style_name_test::normalized_link_manager_cpp();

  ASSERT_TRUE(normalized.find("returnTranslationManager::is_valid_text_composition_style_slug(name);") !=
              td::string::npos);
}
