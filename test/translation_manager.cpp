// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs

#include "td/telegram/TranslationManager.h"

#include "td/utils/tests.h"

namespace {

using td::int64;
using td::TranslationManager;

TEST(TranslationManager, SanitizeAiComposeStylesRejectsUnexpectedFieldCount) {
  auto styles = TranslationManager::sanitize_ai_compose_styles({"formal", "1"}, "test");

  ASSERT_TRUE(styles.empty());
}

TEST(TranslationManager, SanitizeAiComposeStylesDropsMalformedTriplesAndKeepsValidOnes) {
  auto styles = TranslationManager::sanitize_ai_compose_styles(
      {"formal", "1", "Formal", "", "2", "MissingName", "casual", "abc", "InvalidId", "neutral", "3", "Neutral"},
      "test");

  ASSERT_EQ(6u, styles.size());
  ASSERT_EQ("formal", styles[0]);
  ASSERT_EQ("1", styles[1]);
  ASSERT_EQ("Formal", styles[2]);
  ASSERT_EQ("neutral", styles[3]);
  ASSERT_EQ("3", styles[4]);
  ASSERT_EQ("Neutral", styles[5]);
}

TEST(TranslationManager, SanitizeAiComposeStylesRejectsEmptyDescriptionAndInvalidSignedIds) {
  auto styles = TranslationManager::sanitize_ai_compose_styles(
      {"formal", "-17", "Formal", "casual", "xyz", "Casual", "neutral", "7", ""}, "test");

  ASSERT_TRUE(styles.empty());
}

TEST(TranslationManager, SanitizeAiComposeStylesStressPreservesOrderingAcrossSparseInvalidBursts) {
  td::vector<td::string> input;
  input.reserve(300);
  for (int64 i = 0; i < 100; i++) {
    input.push_back(PSTRING() << "style_" << i);
    input.push_back(i % 7 == 0 ? td::string("bad") : PSTRING() << i);
    input.push_back(i % 11 == 0 ? td::string() : PSTRING() << "Label " << i);
  }

  td::vector<td::string> expected;
  expected.reserve(300);
  for (int64 i = 0; i < 100; i++) {
    if (i % 7 != 0 && i % 11 != 0) {
      expected.push_back(PSTRING() << "style_" << i);
      expected.push_back(PSTRING() << i);
      expected.push_back(PSTRING() << "Label " << i);
    }
  }

  auto styles = TranslationManager::sanitize_ai_compose_styles(std::move(input), "stress");

  ASSERT_EQ(expected.size(), styles.size());
  for (size_t i = 0; i < styles.size(); i++) {
    ASSERT_EQ(expected[i], styles[i]);
  }
  ASSERT_TRUE(styles.size() < 300u);
  ASSERT_TRUE(styles.size() > 0u);
}

TEST(TranslationManager, SanitizeAiComposeStylesRetainsLargeSignedIdentifiersVerbatim) {
  auto styles = TranslationManager::sanitize_ai_compose_styles({"formal", "9223372036854775807", "Formal"}, "test");

  ASSERT_EQ(3u, styles.size());
  ASSERT_EQ("formal", styles[0]);
  ASSERT_EQ("9223372036854775807", styles[1]);
  ASSERT_EQ("Formal", styles[2]);
}

}  // namespace