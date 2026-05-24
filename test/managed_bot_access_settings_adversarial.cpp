// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/managed_bot_access_settings_test_utils.h"

TEST(ManagedBotAccessSettingsAdversarial, GetEndpointMustNotBypassBotInfoManagerGuardPath) {
  auto normalized = td::managed_bot_access_settings_test::normalize_for_contract(
      td::managed_bot_access_settings_test::requests_get_managed_bot_access_settings_handler_region());

  ASSERT_NE(td::string::npos, normalized.find(R"(CHECK_IS_BOT();)"));
  ASSERT_NE(td::string::npos,
            normalized.find(
                R"(bot_info_manager_->get_bot_access_settings(UserId(request.bot_user_id_),std::move(promise));)"));
  ASSERT_EQ(td::string::npos, normalized.find(R"(create_handler<GetAccessSettingsQuery>)"));
  ASSERT_EQ(td::string::npos, normalized.find(R"(bots_getAccessSettings)"));
}

TEST(ManagedBotAccessSettingsAdversarial, SetEndpointMustNotBypassBotInfoManagerGuardPath) {
  auto normalized = td::managed_bot_access_settings_test::normalize_for_contract(
      td::managed_bot_access_settings_test::requests_set_managed_bot_access_settings_handler_region());

  ASSERT_NE(td::string::npos, normalized.find(R"(CHECK_IS_BOT();)"));
  ASSERT_NE(
      td::string::npos,
      normalized.find(
          R"(bot_info_manager_->set_bot_access_settings(UserId(request.bot_user_id_),std::move(request.settings_),std::move(promise));)"));
  ASSERT_EQ(td::string::npos, normalized.find(R"(create_handler<EditAccessSettingsQuery>)"));
  ASSERT_EQ(td::string::npos, normalized.find(R"(bots_editAccessSettings)"));
}

TEST(ManagedBotAccessSettingsAdversarial, ReadDispatchMustFailClosedBeforeManagerDelegation) {
  auto normalized_access =
      td::managed_bot_access_settings_test::normalized_managed_bot_access_settings_access_h_source();

  const auto auth_guard =
      R"(if(!is_bot_session){reject_access(std::forward<PromiseT>(promise),Status::Error(400,"Onlybotscanusethemethod"));returnManagedBotAccessSettingsAccessResult::RejectedNonBotSession;})";
  const auto lookup_guard =
      R"(if(bot_data.is_error()){reject_access(std::forward<PromiseT>(promise),bot_data.move_as_error());returnManagedBotAccessSettingsAccessResult::RejectedTargetLookupError;})";
  const auto ownership_guard =
      R"(if(!bot_data.ok().can_be_edited){reject_access(std::forward<PromiseT>(promise),Status::Error(400,"Botmustbeowned"));returnManagedBotAccessSettingsAccessResult::RejectedUnownedBot;})";
  const auto read_delegate = R"(delegate_to_manager(managed_bot_user_id,std::forward<PromiseT>(promise));)";

  auto auth_guard_pos = normalized_access.find(auth_guard);
  auto lookup_guard_pos = normalized_access.find(lookup_guard);
  auto ownership_guard_pos = normalized_access.find(ownership_guard);
  auto read_delegate_pos = normalized_access.find(read_delegate);

  ASSERT_NE(td::string::npos, auth_guard_pos);
  ASSERT_NE(td::string::npos, lookup_guard_pos);
  ASSERT_NE(td::string::npos, ownership_guard_pos);
  ASSERT_NE(td::string::npos, read_delegate_pos);
  ASSERT_TRUE(auth_guard_pos < lookup_guard_pos);
  ASSERT_TRUE(lookup_guard_pos < ownership_guard_pos);
  ASSERT_TRUE(ownership_guard_pos < read_delegate_pos);
}

TEST(ManagedBotAccessSettingsAdversarial, WriteDispatchMustFailClosedBeforeManagerDelegation) {
  auto normalized_access =
      td::managed_bot_access_settings_test::normalized_managed_bot_access_settings_access_h_source();

  const auto auth_guard =
      R"(if(!is_bot_session){reject_access(std::forward<PromiseT>(promise),Status::Error(400,"Onlybotscanusethemethod"));returnManagedBotAccessSettingsAccessResult::RejectedNonBotSession;})";
  const auto lookup_guard =
      R"(if(bot_data.is_error()){reject_access(std::forward<PromiseT>(promise),bot_data.move_as_error());returnManagedBotAccessSettingsAccessResult::RejectedTargetLookupError;})";
  const auto ownership_guard =
      R"(if(!bot_data.ok().can_be_edited){reject_access(std::forward<PromiseT>(promise),Status::Error(400,"Botmustbeowned"));returnManagedBotAccessSettingsAccessResult::RejectedUnownedBot;})";
  const auto write_delegate =
      R"(delegate_to_manager(managed_bot_user_id,std::forward<SettingsT>(settings),std::forward<PromiseT>(promise));)";

  auto auth_guard_pos = normalized_access.find(auth_guard);
  auto lookup_guard_pos = normalized_access.find(lookup_guard);
  auto ownership_guard_pos = normalized_access.find(ownership_guard);
  auto write_delegate_pos = normalized_access.find(write_delegate);

  ASSERT_NE(td::string::npos, auth_guard_pos);
  ASSERT_NE(td::string::npos, lookup_guard_pos);
  ASSERT_NE(td::string::npos, ownership_guard_pos);
  ASSERT_NE(td::string::npos, write_delegate_pos);
  ASSERT_TRUE(auth_guard_pos < lookup_guard_pos);
  ASSERT_TRUE(lookup_guard_pos < ownership_guard_pos);
  ASSERT_TRUE(ownership_guard_pos < write_delegate_pos);
}

TEST(ManagedBotAccessSettingsAdversarial, BotInfoManagerMustNotInlineLegacyFailOpenAccessChecks) {
  auto normalized_get = td::managed_bot_access_settings_test::normalize_for_contract(
      td::managed_bot_access_settings_test::get_bot_access_settings_function_region());
  auto normalized_set = td::managed_bot_access_settings_test::normalize_for_contract(
      td::managed_bot_access_settings_test::set_bot_access_settings_function_region());

  const auto legacy_auth_guard =
      R"(if(!td_->auth_manager_->is_bot()){returnpromise.set_error(Status::Error(400,"Onlybotscanusethemethod"));})";
  const auto legacy_ownership_guard =
      R"(if(!bot_data.can_be_edited){returnpromise.set_error(Status::Error(400,"Botmustbeowned"));})";

  ASSERT_EQ(td::string::npos, normalized_get.find(legacy_auth_guard));
  ASSERT_EQ(td::string::npos, normalized_get.find(legacy_ownership_guard));
  ASSERT_EQ(td::string::npos, normalized_set.find(legacy_auth_guard));
  ASSERT_EQ(td::string::npos, normalized_set.find(legacy_ownership_guard));
}
