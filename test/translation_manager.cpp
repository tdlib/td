// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs

// Pre-include TranslationManager dependencies so the private test seam stays local to the header itself.
#include "td/telegram/MessageEntity.h"  // IWYU pragma: keep
#include "td/telegram/MessageFullId.h"  // IWYU pragma: keep
#include "td/telegram/td_api.h"         // IWYU pragma: keep
#include "td/telegram/telegram_api.h"   // IWYU pragma: keep

#include "td/actor/actor.h"  // IWYU pragma: keep

#include "td/utils/common.h"   // IWYU pragma: keep
#include "td/utils/Promise.h"  // IWYU pragma: keep
#include "td/utils/Status.h"   // IWYU pragma: keep

#define private public
#include "td/telegram/TranslationManager.h"
#undef private

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

TEST(TranslationManager, SanitizeAiComposeStylesDropsOverlyLongTitles) {
  td::string too_long_title(129, 'T');

  auto styles = TranslationManager::sanitize_ai_compose_styles(
      {"AbCdEf12", "1", too_long_title, "tone_custom", "2", "Custom"}, "test");

  ASSERT_EQ(3u, styles.size());
  ASSERT_EQ("tone_custom", styles[0]);
  ASSERT_EQ("2", styles[1]);
  ASSERT_EQ("Custom", styles[2]);
}

TEST(TranslationManager, GetUpdateTextCompositionStylesKeepsValidTriples) {
  TranslationManager manager(nullptr, {});
  manager.ai_compose_styles_ = {"tone_custom", "7", "Custom"};

  auto update = manager.get_update_text_composition_styles();

  ASSERT_EQ(1u, update->styles_.size());
  ASSERT_EQ("tone_custom", update->styles_[0]->name_);
  ASSERT_EQ(7, update->styles_[0]->custom_emoji_id_);
  ASSERT_EQ("Custom", update->styles_[0]->title_);
}

TEST(TranslationManager, GetUpdateTextCompositionStylesDropsStyleNameContainingNulByte) {
  TranslationManager manager(nullptr, {});
  td::string invalid_name = "AbCdEf12";
  invalid_name.push_back('\0');
  invalid_name += "tail";
  manager.ai_compose_styles_ = {std::move(invalid_name), "1", "Title"};

  auto update = manager.get_update_text_composition_styles();

  ASSERT_TRUE(update->styles_.empty());
}

TEST(TranslationManager, GetUpdateTextCompositionStylesDropsNonUtf8Titles) {
  TranslationManager manager(nullptr, {});
  td::string invalid_title(1, static_cast<char>(0xFF));
  manager.ai_compose_styles_ = {"AbCdEf12", "1", std::move(invalid_title)};

  auto update = manager.get_update_text_composition_styles();

  ASSERT_TRUE(update->styles_.empty());
}

TEST(TranslationManager, GetUpdateTextCompositionStylesDropsOverlyLongTitles) {
  TranslationManager manager(nullptr, {});
  td::string too_long_title(129, 'T');
  manager.ai_compose_styles_ = {"AbCdEf12", "1", std::move(too_long_title)};

  auto update = manager.get_update_text_composition_styles();

  ASSERT_TRUE(update->styles_.empty());
}

}  // namespace