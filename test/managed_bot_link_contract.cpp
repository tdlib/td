// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/managed_bot_link_test_utils.h"

#include <array>

TEST(ManagedBotLinkContract, ParsesTMeManagerOnlyLinkIntoDefaultBotUsername) {
  auto parsed = td::managed_bot_link_test::parse_request_managed_bot_link("t.me/newbot/manager");

  ASSERT_EQ("manager", parsed->manager_bot_username_);
  ASSERT_EQ("bot", parsed->suggested_bot_username_);
  ASSERT_EQ("", parsed->suggested_bot_name_);
}

TEST(ManagedBotLinkContract, ParsesTgManagerOnlyLinkIntoDefaultBotUsername) {
  auto parsed = td::managed_bot_link_test::parse_request_managed_bot_link("tg:newbot?manager=managerot&name=asd");

  ASSERT_EQ("managerot", parsed->manager_bot_username_);
  ASSERT_EQ("bot", parsed->suggested_bot_username_);
  ASSERT_EQ("asd", parsed->suggested_bot_name_);
}

TEST(ManagedBotLinkContract, ParsesTgLinkWithExplicitEmptySuggestedUsernameIntoDefaultBotUsername) {
  auto parsed =
      td::managed_bot_link_test::parse_request_managed_bot_link("tg:newbot?manager=managerot&username=&name=asd");

  ASSERT_EQ("managerot", parsed->manager_bot_username_);
  ASSERT_EQ("bot", parsed->suggested_bot_username_);
  ASSERT_EQ("asd", parsed->suggested_bot_name_);
}

TEST(ManagedBotLinkContract, AppendsBotSuffixToShortSuggestedUsername) {
  auto parsed = td::managed_bot_link_test::parse_request_managed_bot_link("t.me/newbot/manager/a?name=asd");

  ASSERT_EQ("manager", parsed->manager_bot_username_);
  ASSERT_EQ("abot", parsed->suggested_bot_username_);
  ASSERT_EQ("asd", parsed->suggested_bot_name_);
}

TEST(ManagedBotLinkContract, KeepsSuggestedUsernameWhenSuffixAlreadyPresentCaseInsensitively) {
  const std::array<td::string, 2> usernames = {{"testBot", "testbOt"}};

  for (const auto &username : usernames) {
    auto parsed = td::managed_bot_link_test::parse_request_managed_bot_link(td::string("t.me/newbot/manager/") +
                                                                            username + "?name=asd");

    ASSERT_EQ("manager", parsed->manager_bot_username_);
    ASSERT_EQ(username, parsed->suggested_bot_username_);
    ASSERT_EQ("asd", parsed->suggested_bot_name_);
  }
}