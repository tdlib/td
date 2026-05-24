// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/managed_bot_link_test_utils.h"

TEST(ManagedBotLinkStress, RepeatedNormalizationAndRoundTripRemainStable) {
  constexpr int kIterations = 3000;

  for (int iteration = 0; iteration < kIterations; iteration++) {
    for (const auto &url : {td::Slice("t.me/newbot/manager?name=asd"), td::Slice("t.me/newbot/manager/?name=asd"),
                            td::Slice("t.me/newbot/manager/a?name=asd")}) {
      auto parsed = td::managed_bot_link_test::parse_request_managed_bot_link(url);
      ASSERT_TRUE(td::managed_bot_link_test::ends_with_bot_case_insensitive(parsed->suggested_bot_username_));

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
  }
}

TEST(ManagedBotLinkStress, RepeatedDirectBuildNormalizationRemainsStable) {
  constexpr int kIterations = 3000;

  for (int iteration = 0; iteration < kIterations; iteration++) {
    for (const auto &raw_username : {td::Slice(""), td::Slice("a")}) {
      auto built = td::managed_bot_link_test::build_request_managed_bot_link("manager", raw_username, "asd",
                                                                             (iteration % 2) == 0);
      ASSERT_TRUE(built.is_ok());

      auto reparsed = td::managed_bot_link_test::parse_request_managed_bot_link(built.ok());
      ASSERT_EQ("manager", reparsed->manager_bot_username_);
      ASSERT_TRUE(td::managed_bot_link_test::ends_with_bot_case_insensitive(reparsed->suggested_bot_username_));
      ASSERT_EQ("asd", reparsed->suggested_bot_name_);
    }
  }
}