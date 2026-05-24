// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/managed_bot_access_settings_test_utils.h"

TEST(ManagedBotAccessSettingsIntegration, RequestsAndBotInfoManagerMustExposeSingleGetAndSetPipeline) {
  auto requests_source = td::managed_bot_access_settings_test::normalized_requests_cpp_source();
  auto bot_info_source = td::managed_bot_access_settings_test::normalized_bot_info_manager_cpp_source();

  ASSERT_EQ(1u, td::managed_bot_access_settings_test::count_substring(
                    requests_source,
                    R"(voidRequests::on_request(uint64id,consttd_api::getManagedBotAccessSettings&request){)"));
  ASSERT_EQ(1u,
            td::managed_bot_access_settings_test::count_substring(
                requests_source, R"(voidRequests::on_request(uint64id,td_api::setManagedBotAccessSettings&request){)"));
  ASSERT_EQ(1u, td::managed_bot_access_settings_test::count_substring(
                    requests_source,
                    R"(bot_info_manager_->get_bot_access_settings(UserId(request.bot_user_id_),std::move(promise));)"));
  ASSERT_EQ(
      1u,
      td::managed_bot_access_settings_test::count_substring(
          requests_source,
          R"(bot_info_manager_->set_bot_access_settings(UserId(request.bot_user_id_),std::move(request.settings_),std::move(promise));)"));

  ASSERT_EQ(
      1u,
      td::managed_bot_access_settings_test::count_substring(
          bot_info_source,
          R"(voidBotInfoManager::get_bot_access_settings(UserIdbot_user_id,Promise<td_api::object_ptr<td_api::botAccessSettings>>&&promise){)"));
  ASSERT_EQ(
      1u,
      td::managed_bot_access_settings_test::count_substring(
          bot_info_source,
          R"(voidBotInfoManager::set_bot_access_settings(UserIdbot_user_id,td_api::object_ptr<td_api::botAccessSettings>&&settings,Promise<Unit>&&promise){)"));
}

TEST(ManagedBotAccessSettingsIntegration, BotInfoManagerQueriesMustUseDedicatedAccessSettingsTypes) {
  auto bot_info_source = td::managed_bot_access_settings_test::normalized_bot_info_manager_cpp_source();

  ASSERT_EQ(1u, td::managed_bot_access_settings_test::count_substring(
                    bot_info_source, R"(classGetAccessSettingsQueryfinal:publicTd::ResultHandler{)"));
  ASSERT_EQ(1u, td::managed_bot_access_settings_test::count_substring(
                    bot_info_source, R"(classEditAccessSettingsQueryfinal:publicTd::ResultHandler{)"));
  ASSERT_EQ(1u, td::managed_bot_access_settings_test::count_substring(
                    bot_info_source, R"(fetch_result<telegram_api::bots_getAccessSettings>(packet);)"));
  ASSERT_EQ(1u, td::managed_bot_access_settings_test::count_substring(
                    bot_info_source, R"(fetch_result<telegram_api::bots_editAccessSettings>(packet);)"));
}

TEST(ManagedBotAccessSettingsIntegration, SchemaAndConversionMustStayAligned) {
  auto td_api_source = td::managed_bot_access_settings_test::normalized_td_api_source();
  auto bot_access_source = td::managed_bot_access_settings_test::normalized_bot_access_settings_cpp_source();

  ASSERT_EQ(
      1u, td::managed_bot_access_settings_test::count_substring(
              td_api_source, R"(botAccessSettingsis_restricted:Booladded_user_ids:vector<int53>=BotAccessSettings;)"));
  ASSERT_EQ(
      1u,
      td::managed_bot_access_settings_test::count_substring(
          bot_access_source,
          R"(returntd_api::make_object<td_api::botAccessSettings>(is_restricted_,td->user_manager_->get_user_ids_object(added_user_ids_,"botAccessSettings"));)"));
}

TEST(ManagedBotAccessSettingsIntegration, EditAccessSettingsQueryMustUseBotAccessSettingsInputUserResolver) {
  auto bot_info_source = td::managed_bot_access_settings_test::normalized_bot_info_manager_cpp_source();

  ASSERT_EQ(
      1u,
      td::managed_bot_access_settings_test::count_substring(
          bot_info_source,
          R"(settings.resolve_added_input_users([&](UserIduser_id){returntd_->user_manager_->get_input_user(user_id);});)"));
  ASSERT_EQ(0u, td::managed_bot_access_settings_test::count_substring(
                    bot_info_source, R"(for(autouser_id:settings.get_added_user_ids()){)"));
}
