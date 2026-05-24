// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/telegram/TranslationManager.h"

#include "td/utils/common.h"
#include "td/utils/utf8.h"

namespace td::w5_text_composition_style_catalog_sanitizer_test {

constexpr size_t STYLE_FIELD_COUNT = 3;
constexpr size_t MAX_STYLE_NAME_LENGTH = 128;
constexpr size_t MAX_STYLE_TITLE_LENGTH = 128;

inline bool contains_nul(Slice value) {
  for (auto c : value) {
    if (c == '\0') {
      return true;
    }
  }
  return false;
}

inline bool is_valid_sanitized_triple(Slice style_name, Slice style_id, Slice style_title) {
  if (style_name.empty() || style_title.empty()) {
    return false;
  }
  if (style_name.size() > MAX_STYLE_NAME_LENGTH) {
    return false;
  }
  if (style_title.size() > MAX_STYLE_TITLE_LENGTH) {
    return false;
  }
  if (contains_nul(style_name) || contains_nul(style_title)) {
    return false;
  }
  if (!check_utf8(style_name.str()) || !check_utf8(style_title.str())) {
    return false;
  }
  auto r_style_id = to_integer_safe<int64>(style_id);
  return r_style_id.is_ok() && r_style_id.ok() > 0;
}

inline bool is_well_formed_sanitized_catalog(const vector<string> &catalog) {
  if (catalog.size() % STYLE_FIELD_COUNT != 0) {
    return false;
  }
  for (size_t i = 0; i < catalog.size(); i += STYLE_FIELD_COUNT) {
    if (!is_valid_sanitized_triple(catalog[i], catalog[i + 1], catalog[i + 2])) {
      return false;
    }
  }
  return true;
}

inline vector<string> sanitize(vector<string> catalog) {
  return TranslationManager::sanitize_ai_compose_styles(std::move(catalog), "test");
}

}  // namespace td::w5_text_composition_style_catalog_sanitizer_test
