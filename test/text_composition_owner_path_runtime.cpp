// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/TextCompositionUpdateDispatch.h"
#include "td/telegram/TranslationManager.h"

#include "test/text_composition_link_test_utils.h"

#include "td/utils/tests.h"

namespace {

TEST(TextCompositionOwnerPathRuntime, NonBotDispatchReloadKeepsValidationAndLinkContractsAligned) {
  td::vector<td::string> sanitized_styles;
  bool known_style_ok = false;
  bool slug_style_ok = false;
  bool malformed_style_rejected = false;
  bool link_round_trip_ok = false;
  td::int32 reload_calls = 0;
  td::int32 complete_calls = 0;

  auto dispatch_result = td::dispatch_ai_compose_tones_update(
      false,
      [&] {
        reload_calls++;
        auto raw_styles =
            td::vector<td::string>{"formal", "1", "Formal", "", "2", "Broken", "tone_custom", "3", "Custom"};
        sanitized_styles =
            td::TranslationManager::sanitize_ai_compose_styles(std::move(raw_styles), "owner_path_runtime");

        known_style_ok =
            td::TranslationManager::validate_text_composition_style_name("tone_custom", sanitized_styles).is_ok();
        slug_style_ok =
            td::TranslationManager::validate_text_composition_style_name("AbCdEf12", sanitized_styles).is_ok();
        malformed_style_rejected =
            td::TranslationManager::validate_text_composition_style_name("AbCd+Ef12", sanitized_styles).is_error();

        auto maybe_link = td::text_composition_link_test::build_text_composition_style_link("AbCdEf12", true);
        link_round_trip_ok = maybe_link.is_ok() && td::text_composition_link_test::is_text_composition_style_link(
                                                       maybe_link.ok(), "AbCdEf12");
      },
      [&] { complete_calls++; });

  ASSERT_EQ(static_cast<int>(td::TextCompositionTonesUpdateDispatchResult::ReloadRequestedAndPromiseCompleted),
            static_cast<int>(dispatch_result));
  ASSERT_EQ(1, reload_calls);
  ASSERT_EQ(1, complete_calls);
  ASSERT_EQ(6u, sanitized_styles.size());
  ASSERT_TRUE(known_style_ok);
  ASSERT_TRUE(slug_style_ok);
  ASSERT_TRUE(malformed_style_rejected);
  ASSERT_TRUE(link_round_trip_ok);
}

TEST(TextCompositionOwnerPathRuntime, BotDispatchSkipsReloadAndCompletesPromiseExactlyOnce) {
  bool reload_called = false;
  td::int32 complete_calls = 0;

  auto dispatch_result =
      td::dispatch_ai_compose_tones_update(true, [&] { reload_called = true; }, [&] { complete_calls++; });

  ASSERT_EQ(static_cast<int>(td::TextCompositionTonesUpdateDispatchResult::SkippedForBotSession),
            static_cast<int>(dispatch_result));
  ASSERT_FALSE(reload_called);
  ASSERT_EQ(1, complete_calls);
}

}  // namespace
