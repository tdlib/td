// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/TranslationManager.h"

#include "td/utils/tests.h"

namespace {

bool is_base64url_char(char c) {
  return td::is_alpha(c) || td::is_digit(c) || c == '-' || c == '_';
}

bool is_base64url(td::Slice value) {
  for (auto c : value) {
    if (!is_base64url_char(c)) {
      return false;
    }
  }
  return true;
}

bool expected_style_status(td::Slice style_name, const td::vector<td::string> &ai_compose_styles) {
  if (style_name.empty()) {
    return true;
  }
  if (style_name == "formal" || style_name == "neutral" || style_name == "casual") {
    return true;
  }
  if (style_name.size() > 128) {
    return false;
  }
  for (auto c : style_name) {
    if (c == '\0') {
      return false;
    }
  }
  if (ai_compose_styles.size() % 3 != 0) {
    return false;
  }
  for (size_t i = 0; i < ai_compose_styles.size(); i += 3) {
    if (ai_compose_styles[i] == style_name) {
      return true;
    }
  }
  return style_name.size() >= 8 && is_base64url(style_name);
}

}  // namespace

TEST(TextCompositionStyleNameLightFuzz, DeterministicMatrixMatchesExpectedGateRules) {
  td::vector<td::string> ai_compose_styles = {"formal",  "1",           "Formal", "neutral", "2",
                                              "Neutral", "tone_custom", "3",      "Custom"};

  td::uint32 seed = 0xC0FFEEu;
  const td::string alphabet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_=+?";

  for (td::int32 i = 0; i < 3000; i++) {
    seed = seed * 1664525u + 1013904223u;
    auto length = static_cast<size_t>((seed % 150u) + 1u);

    td::string candidate;
    candidate.reserve(length);
    for (size_t j = 0; j < length; j++) {
      seed = seed * 1664525u + 1013904223u;
      candidate.push_back(alphabet[seed % alphabet.size()]);
    }

    auto status_a = td::TranslationManager::validate_text_composition_style_name(candidate, ai_compose_styles);
    auto status_b = td::TranslationManager::validate_text_composition_style_name(candidate, ai_compose_styles);

    ASSERT_EQ(status_a.is_ok(), status_b.is_ok());
    ASSERT_EQ(expected_style_status(candidate, ai_compose_styles), status_a.is_ok());
  }
}
