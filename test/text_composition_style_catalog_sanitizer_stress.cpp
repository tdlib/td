// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_style_catalog_sanitizer_test_utils.h"

TEST(TextCompositionStyleCatalogSanitizerStress, LargeCatalogRepeatedSanitizationStaysStableAndBounded) {
  td::vector<td::string> raw_catalog;
  raw_catalog.reserve(9000);

  for (td::int32 i = 0; i < 3000; i++) {
    if (i % 5 == 0) {
      td::string style_name = PSTRING() << "style_" << i;
      td::string style_title = PSTRING() << "Title " << i;
      raw_catalog.push_back(std::move(style_name));
      raw_catalog.push_back(PSTRING() << (i + 1));
      raw_catalog.push_back(std::move(style_title));
    } else if (i % 5 == 1) {
      td::string style_name(129, 'a');
      raw_catalog.push_back(std::move(style_name));
      raw_catalog.push_back(PSTRING() << (i + 1));
      raw_catalog.push_back("TooLong");
    } else if (i % 5 == 2) {
      td::string broken_name = "tone";
      broken_name.push_back('\0');
      broken_name += "_broken";
      raw_catalog.push_back(std::move(broken_name));
      raw_catalog.push_back(PSTRING() << (i + 1));
      raw_catalog.push_back("Broken");
    } else if (i % 5 == 3) {
      raw_catalog.push_back(PSTRING() << "style_bad_id_" << i);
      raw_catalog.push_back("0");
      raw_catalog.push_back("BadId");
    } else {
      raw_catalog.push_back(PSTRING() << "style_title_bad_" << i);
      raw_catalog.push_back(PSTRING() << (i + 1));
      td::string title = "bad";
      title.push_back('\0');
      title += "title";
      raw_catalog.push_back(std::move(title));
    }
  }

  td::uint64 non_empty_runs = 0;
  td::uint64 stable_runs = 0;

  for (td::int32 i = 0; i < 300; i++) {
    auto sanitized_first = td::w5_text_composition_style_catalog_sanitizer_test::sanitize(raw_catalog);
    auto sanitized_second = td::w5_text_composition_style_catalog_sanitizer_test::sanitize(raw_catalog);

    ASSERT_EQ(sanitized_first, sanitized_second);
    ASSERT_TRUE(
        td::w5_text_composition_style_catalog_sanitizer_test::is_well_formed_sanitized_catalog(sanitized_first));
    ASSERT_TRUE(sanitized_first.size() <= raw_catalog.size());

    if (!sanitized_first.empty()) {
      non_empty_runs++;
    }
    stable_runs++;
  }

  ASSERT_EQ(static_cast<td::uint64>(300), stable_runs);
  ASSERT_TRUE(non_empty_runs > 0);
}
