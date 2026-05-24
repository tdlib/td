// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/TranslationManager.h"

#include "td/utils/tests.h"

TEST(TextCompositionStyleNameStress, LargeCatalogAndRepeatedValidationStayStable) {
  td::vector<td::string> ai_compose_styles;
  ai_compose_styles.reserve(3000);
  for (td::int32 i = 0; i < 1000; i++) {
    ai_compose_styles.push_back(PSTRING() << "style_" << i);
    ai_compose_styles.push_back(PSTRING() << (i + 1));
    ai_compose_styles.push_back(PSTRING() << "Style " << i);
  }

  ASSERT_TRUE(td::TranslationManager::validate_text_composition_style_name("style_0", ai_compose_styles).is_ok());
  ASSERT_TRUE(td::TranslationManager::validate_text_composition_style_name("style_999", ai_compose_styles).is_ok());

  td::uint64 accepted = 0;
  td::uint64 rejected = 0;

  for (td::int32 i = 0; i < 50000; i++) {
    td::string candidate;
    if (i % 10 == 0) {
      candidate = PSTRING() << "style_" << (i % 1000);
    } else if (i % 10 == 1) {
      candidate = "AbCdEf12";
    } else if (i % 10 == 2) {
      candidate = "AbCd+Ef12";
    } else if (i % 10 == 3) {
      candidate = "short1";
    } else {
      candidate = PSTRING() << "unknown_" << i;
    }

    if (td::TranslationManager::validate_text_composition_style_name(candidate, ai_compose_styles).is_ok()) {
      accepted++;
    } else {
      rejected++;
    }
  }

  ASSERT_TRUE(accepted > 0);
  ASSERT_TRUE(rejected > 0);
}
