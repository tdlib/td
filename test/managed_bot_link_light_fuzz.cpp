// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include "test/managed_bot_link_test_utils.h"

#include <array>

namespace {

struct ManagedBotCase {
  td::string url;
  td::string expected_manager_username;
  td::string expected_username;
  td::string expected_name;
};

struct ManagedBotBuildCase {
  td::string manager_username;
  td::string raw_username;
  td::string expected_username;
  td::string expected_name;
};

}  // namespace

TEST(ManagedBotLinkLightFuzz, DeterministicManagedBotLinkMatrixNormalizesAndPreservesExpectedValues) {
  const std::array<ManagedBotCase, 9> cases = {{
      {"t.me/newbot/manager", "manager", "bot", ""},
      {"t.me/newbot/manager?name=asd", "manager", "bot", "asd"},
      {"t.me/newbot/manager/?name=asd", "manager", "bot", "asd"},
      {"t.me/newbot/manager/a?name=asd", "manager", "abot", "asd"},
      {"t.me/newbot/manager/ab?name=asd", "manager", "abbot", "asd"},
      {"tg:newbot?manager=managerot&name=asd", "managerot", "bot", "asd"},
      {"tg:newbot?manager=managerot&username=&name=asd", "managerot", "bot", "asd"},
      {"tg:newbot?manager=managerot&username=a", "managerot", "abot", ""},
      {"t.me/newbot/manager/testbOt?name=asd", "manager", "testbOt", "asd"},
  }};

  for (int iteration = 0; iteration < 10000; iteration++) {
    auto index = static_cast<size_t>(td::Random::fast(0, static_cast<int>(cases.size()) - 1));
    const auto &test_case = cases[index];

    auto parsed = td::managed_bot_link_test::parse_request_managed_bot_link(test_case.url);
    ASSERT_EQ(test_case.expected_manager_username, parsed->manager_bot_username_);
    ASSERT_EQ(test_case.expected_username, parsed->suggested_bot_username_);
    ASSERT_EQ(test_case.expected_name, parsed->suggested_bot_name_);
    ASSERT_TRUE(td::managed_bot_link_test::ends_with_bot_case_insensitive(parsed->suggested_bot_username_));
  }
}

TEST(ManagedBotLinkLightFuzz, DeterministicDirectBuildMatrixNormalizesRawSuggestedUsernames) {
  const std::array<ManagedBotBuildCase, 4> cases = {{
      {"manager", "", "bot", "asd"},
      {"manager", "a", "abot", "asd"},
      {"manager", "ab", "abbot", "asd"},
      {"manager", "testbOt", "testbOt", "asd"},
  }};

  for (int iteration = 0; iteration < 10000; iteration++) {
    auto index = static_cast<size_t>(td::Random::fast(0, static_cast<int>(cases.size()) - 1));
    const auto &test_case = cases[index];

    auto built = td::managed_bot_link_test::build_request_managed_bot_link(
        test_case.manager_username, test_case.raw_username, test_case.expected_name, (iteration % 2) == 0);
    ASSERT_TRUE(built.is_ok());

    auto parsed = td::managed_bot_link_test::parse_request_managed_bot_link(built.ok());
    ASSERT_EQ(test_case.manager_username, parsed->manager_bot_username_);
    ASSERT_EQ(test_case.expected_username, parsed->suggested_bot_username_);
    ASSERT_EQ(test_case.expected_name, parsed->suggested_bot_name_);
    ASSERT_TRUE(td::managed_bot_link_test::ends_with_bot_case_insensitive(parsed->suggested_bot_username_));
  }
}