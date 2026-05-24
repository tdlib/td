// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/BotAccessSettings.h"

#include "td/utils/Random.h"
#include "td/utils/tests.h"

namespace {

td::telegram_api::object_ptr<td::telegram_api::User> make_server_user(td::int64 user_id) {
  auto user = td::telegram_api::make_object<td::telegram_api::user>();
  user->id_ = user_id;
  return user;
}

td::telegram_api::object_ptr<td::telegram_api::bots_accessSettings> make_server_access_settings(
    bool restricted, const td::vector<td::int64> &added_user_ids) {
  auto settings = td::telegram_api::make_object<td::telegram_api::bots_accessSettings>();
  settings->restricted_ = restricted;
  settings->add_users_.reserve(added_user_ids.size());
  for (auto user_id : added_user_ids) {
    settings->add_users_.push_back(make_server_user(user_id));
  }
  return settings;
}

struct ParsedServerAccessSettings {
  td::Status validation_status;
  bool restricted{false};
  size_t added_user_count{0};
};

ParsedServerAccessSettings parse_server_access_settings(
    td::telegram_api::object_ptr<td::telegram_api::bots_accessSettings> &&server_settings) {
  td::BotAccessSettings access_settings(nullptr, std::move(server_settings));

  ParsedServerAccessSettings parsed;
  parsed.validation_status = access_settings.get_validation_status();
  parsed.restricted = access_settings.is_restricted();
  parsed.added_user_count = access_settings.get_added_user_ids().size();
  return parsed;
}

}  // namespace

TEST(ManagedBotAccessSettingsServerPayloadRuntime, AcceptsRestrictedPayloadWithoutAddedUsers) {
  auto parsed = parse_server_access_settings(make_server_access_settings(true, td::vector<td::int64>{}));

  ASSERT_FALSE(parsed.validation_status.is_error());
  ASSERT_TRUE(parsed.restricted);
  ASSERT_EQ(0u, parsed.added_user_count);
}

TEST(ManagedBotAccessSettingsServerPayloadRuntime, RejectsNonRestrictedPayloadWithAddedUsersFailClosed) {
  auto parsed = parse_server_access_settings(make_server_access_settings(false, td::vector<td::int64>{101, 202}));

  ASSERT_TRUE(parsed.validation_status.is_error());
  ASSERT_EQ(500, parsed.validation_status.code());
  ASSERT_EQ("Receive added users in a non-restricted bot", parsed.validation_status.message().str());
  ASSERT_FALSE(parsed.restricted);
  ASSERT_EQ(0u, parsed.added_user_count);
}

TEST(ManagedBotAccessSettingsServerPayloadRuntime, RejectsRestrictedPayloadContainingInvalidUserId) {
  auto parsed = parse_server_access_settings(make_server_access_settings(true, td::vector<td::int64>{0}));

  ASSERT_TRUE(parsed.validation_status.is_error());
  ASSERT_EQ(500, parsed.validation_status.code());
  ASSERT_EQ("Receive invalid added user in bot access settings", parsed.validation_status.message().str());
  ASSERT_TRUE(parsed.restricted);
  ASSERT_EQ(0u, parsed.added_user_count);
}

TEST(ManagedBotAccessSettingsServerPayloadRuntime, RejectsMixedPayloadWithoutPartialAcceptance) {
  auto parsed = parse_server_access_settings(make_server_access_settings(true, td::vector<td::int64>{0, 707, 909}));

  ASSERT_TRUE(parsed.validation_status.is_error());
  ASSERT_EQ(500, parsed.validation_status.code());
  ASSERT_EQ("Receive invalid added user in bot access settings", parsed.validation_status.message().str());
  ASSERT_EQ(0u, parsed.added_user_count);
}

TEST(ManagedBotAccessSettingsServerPayloadRuntime, DeterministicMalformedPayloadFuzzAlwaysFailsClosed) {
  for (int iteration = 0; iteration < 10000; iteration++) {
    auto restricted = td::Random::fast(0, 1) == 1;
    auto user_count = static_cast<size_t>(td::Random::fast(1, 24));

    td::vector<td::int64> user_ids;
    user_ids.reserve(user_count);
    if (restricted) {
      user_ids.push_back(0);
      for (size_t i = 1; i < user_count; i++) {
        user_ids.push_back(td::Random::fast(-16, 16));
      }
    } else {
      for (size_t i = 0; i < user_count; i++) {
        user_ids.push_back(td::Random::fast(-16, 16));
      }
    }

    auto parsed = parse_server_access_settings(make_server_access_settings(restricted, user_ids));

    ASSERT_TRUE(parsed.validation_status.is_error());
    ASSERT_EQ(500, parsed.validation_status.code());
    ASSERT_EQ(0u, parsed.added_user_count);
  }
}