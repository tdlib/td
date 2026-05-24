// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/TextCompositionUpdateDispatch.h"

#include "td/utils/tests.h"

#include "test/text_composition_link_test_utils.h"

#include <cstdint>
#include <vector>

namespace {

enum class RuntimeEvent : std::uint8_t {
  Reload,
  Complete,
};

TEST(TextCompositionRuntimeHarness, BotSessionCompletesWithoutReloadingStyles) {
  std::size_t reload_calls = 0;
  std::size_t completion_calls = 0;

  auto result = td::dispatch_ai_compose_tones_update(true, [&] { reload_calls++; }, [&] { completion_calls++; });

  ASSERT_EQ(static_cast<int>(td::TextCompositionTonesUpdateDispatchResult::SkippedForBotSession),
            static_cast<int>(result));
  ASSERT_EQ(0u, reload_calls);
  ASSERT_EQ(1u, completion_calls);
}

TEST(TextCompositionRuntimeHarness, UserSessionReloadsBeforePromiseCompletion) {
  std::vector<RuntimeEvent> events;

  auto result = td::dispatch_ai_compose_tones_update(
      false, [&] { events.push_back(RuntimeEvent::Reload); }, [&] { events.push_back(RuntimeEvent::Complete); });

  ASSERT_EQ(static_cast<int>(td::TextCompositionTonesUpdateDispatchResult::ReloadRequestedAndPromiseCompleted),
            static_cast<int>(result));
  ASSERT_EQ(2u, events.size());
  ASSERT_EQ(static_cast<int>(RuntimeEvent::Reload), static_cast<int>(events[0]));
  ASSERT_EQ(static_cast<int>(RuntimeEvent::Complete), static_cast<int>(events[1]));
}

TEST(TextCompositionRuntimeHarness, UserSessionReloadPathKeepsTextCompositionDeepLinkRoundTripValid) {
  std::vector<RuntimeEvent> events;
  bool internal_round_trip_ok = false;
  bool external_round_trip_ok = false;

  auto result = td::dispatch_ai_compose_tones_update(
      false,
      [&] {
        events.push_back(RuntimeEvent::Reload);

        auto internal_link = td::text_composition_link_test::build_text_composition_style_link("AbCdEf12", true);
        internal_round_trip_ok =
            internal_link.is_ok() &&
            td::text_composition_link_test::is_text_composition_style_link(internal_link.ok(), "AbCdEf12");

        auto external_link = td::text_composition_link_test::build_text_composition_style_link("AbCdEf12", false);
        external_round_trip_ok =
            external_link.is_ok() &&
            td::text_composition_link_test::is_text_composition_style_link(external_link.ok(), "AbCdEf12");
      },
      [&] { events.push_back(RuntimeEvent::Complete); });

  ASSERT_EQ(static_cast<int>(td::TextCompositionTonesUpdateDispatchResult::ReloadRequestedAndPromiseCompleted),
            static_cast<int>(result));
  ASSERT_TRUE(internal_round_trip_ok);
  ASSERT_TRUE(external_round_trip_ok);
  ASSERT_EQ(2u, events.size());
  ASSERT_EQ(static_cast<int>(RuntimeEvent::Reload), static_cast<int>(events[0]));
  ASSERT_EQ(static_cast<int>(RuntimeEvent::Complete), static_cast<int>(events[1]));
}

TEST(TextCompositionRuntimeHarness, DuplicateSlugQueryStaysFailClosedAsUnknownDeepLink) {
  ASSERT_TRUE(td::text_composition_link_test::is_unknown_deep_link("tg:addstyle?slug=AbCdEf12&slug=QwErTy34"));
  ASSERT_TRUE(!td::text_composition_link_test::is_text_composition_style_link("tg:addstyle?slug=AbCdEf12&slug=QwErTy34",
                                                                              "AbCdEf12"));
}

}  // namespace