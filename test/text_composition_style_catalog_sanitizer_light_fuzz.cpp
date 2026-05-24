// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/text_composition_style_catalog_sanitizer_test_utils.h"

namespace {

td::uint32 next_seed(td::uint32 seed) {
  return seed * 1664525u + 1013904223u;
}

td::string random_field(td::uint32 &seed, size_t max_len) {
  static const td::string alphabet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_=+?/\\x";

  seed = next_seed(seed);
  size_t len = static_cast<size_t>(seed % (max_len + 1));
  td::string out;
  out.reserve(len + 1);
  for (size_t i = 0; i < len; i++) {
    seed = next_seed(seed);
    out.push_back(alphabet[seed % alphabet.size()]);
  }
  return out;
}

}  // namespace

TEST(TextCompositionStyleCatalogSanitizerLightFuzz, DeterministicMatrixPreservesSanitizationInvariants) {
  td::uint32 seed = 0xBADC0DEu;

  for (td::int32 i = 0; i < 2000; i++) {
    td::vector<td::string> raw_catalog;

    seed = next_seed(seed);
    size_t triple_count = static_cast<size_t>(seed % 12u);
    raw_catalog.reserve(triple_count * td::w5_text_composition_style_catalog_sanitizer_test::STYLE_FIELD_COUNT);

    for (size_t j = 0; j < triple_count; j++) {
      auto style_name = random_field(seed, 150);
      auto style_id = PSTRING() << static_cast<td::int64>((seed % 7u) - 3u);
      auto style_title = random_field(seed, 180);

      seed = next_seed(seed);
      if ((seed % 10u) == 0u && !style_name.empty()) {
        style_name[0] = '\0';
      }
      seed = next_seed(seed);
      if ((seed % 10u) == 1u && !style_title.empty()) {
        style_title[0] = '\0';
      }

      raw_catalog.push_back(std::move(style_name));
      raw_catalog.push_back(std::move(style_id));
      raw_catalog.push_back(std::move(style_title));
    }

    auto sanitized_a = td::w5_text_composition_style_catalog_sanitizer_test::sanitize(raw_catalog);
    auto sanitized_b = td::w5_text_composition_style_catalog_sanitizer_test::sanitize(raw_catalog);

    ASSERT_EQ(sanitized_a, sanitized_b);
    ASSERT_TRUE(td::w5_text_composition_style_catalog_sanitizer_test::is_well_formed_sanitized_catalog(sanitized_a));
  }
}
