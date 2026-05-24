// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/managed_bot_access_settings_test_utils.h"

TEST(ManagedBotAccessSettingsStress, RepeatedContractExtractionMustRemainStable) {
  constexpr int kIterations = 3000;

  for (int iteration = 0; iteration < kIterations; iteration++) {
    const auto normalized_td_api = td::managed_bot_access_settings_test::normalized_td_api_source();
    const auto normalized_telegram_api = td::managed_bot_access_settings_test::normalized_telegram_api_source();
    const auto normalized_requests = td::managed_bot_access_settings_test::normalized_requests_cpp_source();
    const auto normalized_get = td::managed_bot_access_settings_test::normalize_for_contract(
        td::managed_bot_access_settings_test::get_bot_access_settings_function_region());
    const auto normalized_set = td::managed_bot_access_settings_test::normalize_for_contract(
        td::managed_bot_access_settings_test::set_bot_access_settings_function_region());
    const auto normalized_access =
        td::managed_bot_access_settings_test::normalized_managed_bot_access_settings_access_h_source();

    auto td_api_object_pos =
        normalized_td_api.find(R"(botAccessSettingsis_restricted:Booladded_user_ids:vector<int53>=BotAccessSettings;)");
    auto td_api_get_pos = normalized_td_api.find(R"(getManagedBotAccessSettingsbot_user_id:int53=BotAccessSettings;)");
    auto td_api_set_pos =
        normalized_td_api.find(R"(setManagedBotAccessSettingsbot_user_id:int53settings:botAccessSettings=Ok;)");

    auto telegram_type_pos = normalized_telegram_api.find(
        R"(bots.accessSettings#dd1fbf93flags:#restricted:flags.0?trueadd_users:flags.1?Vector<User>=bots.AccessSettings;)");
    auto telegram_get_pos =
        normalized_telegram_api.find(R"(bots.getAccessSettings#213853a3bot:InputUser=bots.AccessSettings;)");
    auto telegram_set_pos = normalized_telegram_api.find(
        R"(bots.editAccessSettings#31813cd8flags:#restricted:flags.0?truebot:InputUseradd_users:flags.1?Vector<InputUser>=Bool;)");

    auto get_handler_pos = normalized_requests.find(
        R"(bot_info_manager_->get_bot_access_settings(UserId(request.bot_user_id_),std::move(promise));)");
    auto set_handler_pos = normalized_requests.find(
        R"(bot_info_manager_->set_bot_access_settings(UserId(request.bot_user_id_),std::move(request.settings_),std::move(promise));)");

    auto get_seam_pos = normalized_get.find(
        "dispatch_managed_bot_access_settings_read(td_->auth_manager_->is_bot(),bot_user_id.get(),std::move(promise),");
    auto set_seam_pos = normalized_set.find(
        "dispatch_managed_bot_access_settings_write(td_->auth_manager_->is_bot(),bot_user_id.get(),std::move(access_"
        "settings),std::move(promise),");

    auto legacy_get_auth_guard_pos = normalized_get.find(
        R"(if(!td_->auth_manager_->is_bot()){returnpromise.set_error(Status::Error(400,"Onlybotscanusethemethod"));})");
    auto legacy_set_auth_guard_pos = normalized_set.find(
        R"(if(!td_->auth_manager_->is_bot()){returnpromise.set_error(Status::Error(400,"Onlybotscanusethemethod"));})");
    auto legacy_get_owner_guard_pos = normalized_get.find(
        R"(if(!bot_data.can_be_edited){returnpromise.set_error(Status::Error(400,"Botmustbeowned"));})");
    auto legacy_set_owner_guard_pos = normalized_set.find(
        R"(if(!bot_data.can_be_edited){returnpromise.set_error(Status::Error(400,"Botmustbeowned"));})");

    auto access_auth_guard_pos = normalized_access.find(
        R"(if(!is_bot_session){reject_access(std::forward<PromiseT>(promise),Status::Error(400,"Onlybotscanusethemethod"));returnManagedBotAccessSettingsAccessResult::RejectedNonBotSession;})");
    auto access_lookup_guard_pos = normalized_access.find(
        R"(if(bot_data.is_error()){reject_access(std::forward<PromiseT>(promise),bot_data.move_as_error());returnManagedBotAccessSettingsAccessResult::RejectedTargetLookupError;})");
    auto access_owner_guard_pos = normalized_access.find(
        R"(if(!bot_data.ok().can_be_edited){reject_access(std::forward<PromiseT>(promise),Status::Error(400,"Botmustbeowned"));returnManagedBotAccessSettingsAccessResult::RejectedUnownedBot;})");
    auto access_read_delegate_pos =
        normalized_access.find(R"(delegate_to_manager(managed_bot_user_id,std::forward<PromiseT>(promise));)");
    auto access_write_delegate_pos = normalized_access.find(
        R"(delegate_to_manager(managed_bot_user_id,std::forward<SettingsT>(settings),std::forward<PromiseT>(promise));)");

    ASSERT_NE(td::string::npos, td_api_object_pos);
    ASSERT_NE(td::string::npos, td_api_get_pos);
    ASSERT_NE(td::string::npos, td_api_set_pos);
    ASSERT_NE(td::string::npos, telegram_type_pos);
    ASSERT_NE(td::string::npos, telegram_get_pos);
    ASSERT_NE(td::string::npos, telegram_set_pos);
    ASSERT_NE(td::string::npos, get_handler_pos);
    ASSERT_NE(td::string::npos, set_handler_pos);
    ASSERT_NE(td::string::npos, get_seam_pos);
    ASSERT_NE(td::string::npos, set_seam_pos);
    ASSERT_NE(td::string::npos, access_auth_guard_pos);
    ASSERT_NE(td::string::npos, access_lookup_guard_pos);
    ASSERT_NE(td::string::npos, access_owner_guard_pos);
    ASSERT_NE(td::string::npos, access_read_delegate_pos);
    ASSERT_NE(td::string::npos, access_write_delegate_pos);

    ASSERT_EQ(td::string::npos, legacy_get_auth_guard_pos);
    ASSERT_EQ(td::string::npos, legacy_set_auth_guard_pos);
    ASSERT_EQ(td::string::npos, legacy_get_owner_guard_pos);
    ASSERT_EQ(td::string::npos, legacy_set_owner_guard_pos);

    ASSERT_TRUE(td_api_object_pos < td_api_get_pos);
    ASSERT_TRUE(td_api_get_pos < td_api_set_pos);
    ASSERT_TRUE(telegram_type_pos < telegram_get_pos);
    ASSERT_TRUE(telegram_get_pos < telegram_set_pos);
    ASSERT_TRUE(access_auth_guard_pos < access_lookup_guard_pos);
    ASSERT_TRUE(access_lookup_guard_pos < access_owner_guard_pos);
    ASSERT_TRUE(access_owner_guard_pos < access_read_delegate_pos);
    ASSERT_TRUE(access_owner_guard_pos < access_write_delegate_pos);

    ASSERT_EQ(1u,
              td::managed_bot_access_settings_test::count_substring(
                  normalized_requests,
                  R"(bot_info_manager_->get_bot_access_settings(UserId(request.bot_user_id_),std::move(promise));)"));
    ASSERT_EQ(
        1u,
        td::managed_bot_access_settings_test::count_substring(
            normalized_requests,
            R"(bot_info_manager_->set_bot_access_settings(UserId(request.bot_user_id_),std::move(request.settings_),std::move(promise));)"));
    ASSERT_EQ(1u, td::managed_bot_access_settings_test::count_substring(
                      normalized_get,
                      "dispatch_managed_bot_access_settings_read(td_->auth_manager_->is_bot(),bot_user_id.get(),std::"
                      "move(promise),"));
    ASSERT_EQ(1u, td::managed_bot_access_settings_test::count_substring(
                      normalized_set,
                      "dispatch_managed_bot_access_settings_write(td_->auth_manager_->is_bot(),bot_user_id.get(),std::"
                      "move(access_settings),std::move(promise),"));
  }
}
