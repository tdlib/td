// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/managed_bot_link_test_utils.h"

namespace {

void assert_round_trip(td::Slice url, td::Slice expected_username, td::Slice expected_name) {
  auto parsed = td::managed_bot_link_test::parse_request_managed_bot_link(url);
  ASSERT_EQ(expected_username.str(), parsed->suggested_bot_username_);
  ASSERT_EQ(expected_name.str(), parsed->suggested_bot_name_);

  for (auto is_internal : {true, false}) {
    auto built = td::managed_bot_link_test::build_request_managed_bot_link(
        parsed->manager_bot_username_, parsed->suggested_bot_username_, parsed->suggested_bot_name_, is_internal);
    ASSERT_TRUE(built.is_ok());

    auto reparsed = td::managed_bot_link_test::parse_request_managed_bot_link(built.ok());
    ASSERT_EQ(parsed->manager_bot_username_, reparsed->manager_bot_username_);
    ASSERT_EQ(parsed->suggested_bot_username_, reparsed->suggested_bot_username_);
    ASSERT_EQ(parsed->suggested_bot_name_, reparsed->suggested_bot_name_);
  }
}

}  // namespace

TEST(ManagedBotLinkIntegration, ManagerOnlyLinksRoundTripThroughGeneratedLinks) {
  assert_round_trip("t.me/newbot/manager?name=asd", "bot", "asd");
}

TEST(ManagedBotLinkIntegration, ShortSuggestedUsernameRoundTripsAfterNormalization) {
  assert_round_trip("t.me/newbot/manager/a?name=asd", "abot", "asd");
}