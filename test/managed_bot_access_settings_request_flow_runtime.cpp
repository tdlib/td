// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/BotAccessSettings.h"

#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include <algorithm>

namespace {

bool is_test_resolvable_user(td::UserId user_id) {
  return user_id.get() % 2 == 0 || user_id.get() == 3;
}

td::Result<td::telegram_api::object_ptr<td::telegram_api::InputUser>> make_test_input_user(td::UserId user_id) {
  return td::telegram_api::make_object<td::telegram_api::inputUser>(user_id.get(), 0);
}

td::vector<td::int64> get_canonical_added_user_ids(const td::BotAccessSettings &access_settings) {
  td::vector<td::int64> canonical_added_user_ids;
  canonical_added_user_ids.reserve(access_settings.get_added_user_ids().size());
  for (auto user_id : access_settings.get_added_user_ids()) {
    canonical_added_user_ids.push_back(user_id.get());
  }
  return canonical_added_user_ids;
}

template <class IsResolvable>
td::vector<td::int64> build_expected_resolver_calls(const td::vector<td::int64> &canonical_added_user_ids,
                                                    IsResolvable &&is_resolvable) {
  td::vector<td::int64> expected_resolver_calls;
  expected_resolver_calls.reserve(canonical_added_user_ids.size());
  for (auto added_user_id : canonical_added_user_ids) {
    expected_resolver_calls.push_back(added_user_id);
    if (!is_resolvable(td::UserId(added_user_id))) {
      break;
    }
  }
  return expected_resolver_calls;
}

}  // namespace

TEST(ManagedBotAccessSettingsRequestFlowRuntime, RejectsNullSettingsInsteadOfDefaultingToUnrestrictedAccess) {
  td::BotAccessSettings access_settings(td::td_api::object_ptr<td::td_api::botAccessSettings>{});

  auto status = access_settings.get_validation_status();
  ASSERT_TRUE(status.is_error());
  ASSERT_EQ(400, status.code());
  ASSERT_EQ("Managed bot access settings must be specified", status.message().str());
}

TEST(ManagedBotAccessSettingsRequestFlowRuntime, RejectsNullSettingsWhenResolvingAddedInputUsersForEditRequest) {
  td::BotAccessSettings access_settings(td::td_api::object_ptr<td::td_api::botAccessSettings>{});
  auto r_input_users =
      access_settings.resolve_added_input_users([](td::UserId user_id) { return make_test_input_user(user_id); });

  ASSERT_TRUE(r_input_users.is_error());
  ASSERT_EQ(400, r_input_users.error().code());
  ASSERT_EQ("Managed bot access settings must be specified", r_input_users.error().message().str());
}

TEST(ManagedBotAccessSettingsRequestFlowRuntime,
     CanonicalizesDuplicateAddedUsersBeforeInputUserResolutionForEditAccessSettings) {
  td::BotAccessSettings access_settings(
      td::td_api::make_object<td::td_api::botAccessSettings>(true, td::vector<td::int64>{11, 11, 22, 22, 33}));

  td::vector<td::int64> resolver_calls;
  auto r_input_users = access_settings.resolve_added_input_users([&](td::UserId user_id) {
    resolver_calls.push_back(user_id.get());
    return make_test_input_user(user_id);
  });

  ASSERT_TRUE(r_input_users.is_ok());
  ASSERT_EQ((td::vector<td::int64>{11, 22, 33}), resolver_calls);
  ASSERT_EQ(3u, r_input_users.move_as_ok().size());
}

TEST(ManagedBotAccessSettingsRequestFlowRuntime, RejectsUnresolvableUsersDuringInputUserResolution) {
  td::BotAccessSettings access_settings(
      td::td_api::make_object<td::td_api::botAccessSettings>(true, td::vector<td::int64>{44, 55, 44, 66, 55}));

  td::vector<td::int64> resolver_calls;
  auto r_input_users = access_settings.resolve_added_input_users([&](td::UserId user_id) {
    resolver_calls.push_back(user_id.get());
    if (user_id.get() == 55) {
      return td::Result<td::telegram_api::object_ptr<td::telegram_api::InputUser>>(
          td::Status::Error(400, "Failed to resolve added user while building request"));
    }
    return make_test_input_user(user_id);
  });

  ASSERT_TRUE(r_input_users.is_error());
  ASSERT_EQ(400, r_input_users.error().code());
  ASSERT_EQ("Failed to resolve added user while building request", r_input_users.error().message().str());
  ASSERT_EQ((td::vector<td::int64>{44, 55}), resolver_calls);
}

TEST(ManagedBotAccessSettingsRequestFlowRuntime, RejectsAddedUsersWhenRestrictionIsDisabled) {
  td::BotAccessSettings access_settings(
      td::td_api::make_object<td::td_api::botAccessSettings>(false, td::vector<td::int64>{77, 88, -5, 77}));

  auto status = access_settings.get_validation_status();
  ASSERT_TRUE(status.is_error());
  ASSERT_EQ(400, status.code());
  ASSERT_EQ("Added users can be specified only for restricted bot access settings", status.message().str());
}

TEST(ManagedBotAccessSettingsRequestFlowRuntime, RejectsInvalidAddedUsersInsteadOfSilentlyDroppingThem) {
  td::BotAccessSettings access_settings(
      td::td_api::make_object<td::td_api::botAccessSettings>(true, td::vector<td::int64>{0, -1, -999}));

  auto status = access_settings.get_validation_status();
  ASSERT_TRUE(status.is_error());
  ASSERT_EQ(400, status.code());
  ASSERT_EQ("Invalid added user identifier specified", status.message().str());
}

TEST(ManagedBotAccessSettingsRequestFlowRuntime, RejectsPartiallyInvalidAddedUsersInsteadOfAcceptingSubset) {
  td::BotAccessSettings access_settings(
      td::td_api::make_object<td::td_api::botAccessSettings>(true, td::vector<td::int64>{101, 0, 202}));

  auto status = access_settings.get_validation_status();
  ASSERT_TRUE(status.is_error());
  ASSERT_EQ(400, status.code());
  ASSERT_EQ("Invalid added user identifier specified", status.message().str());
}

TEST(ManagedBotAccessSettingsRequestFlowRuntime, DeterministicFuzzMaintainsCanonicalAndFailClosedRequestUsers) {
  for (int iteration = 0; iteration < 10000; iteration++) {
    td::vector<td::int64> raw_added_user_ids;
    raw_added_user_ids.reserve(64);
    for (int i = 0; i < 64; i++) {
      raw_added_user_ids.push_back(td::Random::fast(-8, 24));
    }

    auto is_restricted = td::Random::fast(0, 1) == 1;

    td::BotAccessSettings access_settings(td::td_api::make_object<td::td_api::botAccessSettings>(
        is_restricted, td::vector<td::int64>(raw_added_user_ids)));
    auto status = access_settings.get_validation_status();

    auto has_invalid_user_id = std::any_of(raw_added_user_ids.begin(), raw_added_user_ids.end(),
                                           [](td::int64 user_id) { return !td::UserId(user_id).is_valid(); });
    auto canonical_added_user_ids = get_canonical_added_user_ids(access_settings);
    auto has_unresolvable_valid_user = false;
    for (auto added_user_id : canonical_added_user_ids) {
      if (!is_test_resolvable_user(td::UserId(added_user_id))) {
        has_unresolvable_valid_user = true;
        break;
      }
    }

    auto should_reject =
        has_invalid_user_id || (!is_restricted && !raw_added_user_ids.empty()) || has_unresolvable_valid_user;

    if (should_reject) {
      if (has_invalid_user_id || (!is_restricted && !raw_added_user_ids.empty())) {
        ASSERT_TRUE(status.is_error());
        ASSERT_EQ(400, status.code());
      } else {
        ASSERT_FALSE(status.is_error());
        td::vector<td::int64> resolver_calls;
        auto r_input_users = access_settings.resolve_added_input_users([&](td::UserId user_id) {
          resolver_calls.push_back(user_id.get());
          if (!is_test_resolvable_user(user_id)) {
            return td::Result<td::telegram_api::object_ptr<td::telegram_api::InputUser>>(
                td::Status::Error(400, "Failed to resolve added user while building request"));
          }
          return make_test_input_user(user_id);
        });
        ASSERT_TRUE(r_input_users.is_error());
        ASSERT_EQ(400, r_input_users.error().code());
        ASSERT_EQ("Failed to resolve added user while building request", r_input_users.error().message().str());
        ASSERT_EQ(build_expected_resolver_calls(canonical_added_user_ids, is_test_resolvable_user), resolver_calls);
      }
      continue;
    }

    ASSERT_FALSE(status.is_error());
    td::vector<td::int64> resolver_calls;
    auto r_input_users = access_settings.resolve_added_input_users([&](td::UserId user_id) {
      resolver_calls.push_back(user_id.get());
      return make_test_input_user(user_id);
    });
    ASSERT_TRUE(r_input_users.is_ok());
    ASSERT_EQ(canonical_added_user_ids, resolver_calls);
    ASSERT_EQ(canonical_added_user_ids.size(), r_input_users.move_as_ok().size());
  }
}