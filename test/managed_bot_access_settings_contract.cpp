// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/managed_bot_access_settings_test_utils.h"

TEST(ManagedBotAccessSettingsContract, TdApiMustExposeObjectAndManagedEndpoints) {
  auto normalized = td::managed_bot_access_settings_test::normalized_td_api_source();

  ASSERT_TRUE(
      normalized.find(R"(botAccessSettingsis_restricted:Booladded_user_ids:vector<int53>=BotAccessSettings;)") !=
      td::string::npos);
  ASSERT_TRUE(normalized.find(R"(getManagedBotAccessSettingsbot_user_id:int53=BotAccessSettings;)") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find(R"(setManagedBotAccessSettingsbot_user_id:int53settings:botAccessSettings=Ok;)") !=
              td::string::npos);
}

TEST(ManagedBotAccessSettingsContract, TelegramApiSchemaMustExposeAccessSettingsConstructors) {
  auto normalized = td::managed_bot_access_settings_test::normalized_telegram_api_source();

  ASSERT_TRUE(
      normalized.find(
          R"(bots.accessSettings#dd1fbf93flags:#restricted:flags.0?trueadd_users:flags.1?Vector<User>=bots.AccessSettings;)") !=
      td::string::npos);
  ASSERT_TRUE(normalized.find(R"(bots.getAccessSettings#213853a3bot:InputUser=bots.AccessSettings;)") !=
              td::string::npos);
  ASSERT_TRUE(
      normalized.find(
          R"(bots.editAccessSettings#31813cd8flags:#restricted:flags.0?truebot:InputUseradd_users:flags.1?Vector<InputUser>=Bool;)") !=
      td::string::npos);
}

TEST(ManagedBotAccessSettingsContract, RequestsHeaderMustDeclareManagedAccessSettingsHandlers) {
  auto normalized = td::managed_bot_access_settings_test::normalized_requests_h_source();

  ASSERT_TRUE(normalized.find(R"(voidon_request(uint64id,consttd_api::getManagedBotAccessSettings&request);)") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find(R"(voidon_request(uint64id,td_api::setManagedBotAccessSettings&request);)") !=
              td::string::npos);
}

TEST(ManagedBotAccessSettingsContract, BotInfoManagerHeaderMustDeclareManagedAccessSettingsMethods) {
  auto normalized = td::managed_bot_access_settings_test::normalized_bot_info_manager_h_source();

  ASSERT_TRUE(
      normalized.find(
          R"(voidget_bot_access_settings(UserIdbot_user_id,Promise<td_api::object_ptr<td_api::botAccessSettings>>&&promise);)") !=
      td::string::npos);
  ASSERT_TRUE(
      normalized.find(
          R"(voidset_bot_access_settings(UserIdbot_user_id,td_api::object_ptr<td_api::botAccessSettings>&&settings,Promise<Unit>&&promise);)") !=
      td::string::npos);
}

TEST(ManagedBotAccessSettingsContract, BotAccessSettingsMustProvideBothReadAndWriteConversions) {
  auto normalized_h = td::managed_bot_access_settings_test::normalized_bot_access_settings_h_source();
  auto normalized_cpp = td::managed_bot_access_settings_test::normalized_bot_access_settings_cpp_source();

  ASSERT_TRUE(normalized_h.find(R"(classBotAccessSettings{)") != td::string::npos);
  ASSERT_TRUE(
      normalized_h.find(
          R"(BotAccessSettings(Td*td,telegram_api::object_ptr<telegram_api::bots_accessSettings>&&settings);)") !=
      td::string::npos);
  ASSERT_TRUE(
      normalized_h.find(R"(explicitBotAccessSettings(td_api::object_ptr<td_api::botAccessSettings>&&settings);)") !=
      td::string::npos);
  ASSERT_TRUE(
      normalized_cpp.find(
          R"(BotAccessSettings::BotAccessSettings(Td*td,telegram_api::object_ptr<telegram_api::bots_accessSettings>&&settings){)") !=
      td::string::npos);
  ASSERT_TRUE(
      normalized_cpp.find(
          R"(BotAccessSettings::BotAccessSettings(td_api::object_ptr<td_api::botAccessSettings>&&settings){)") !=
      td::string::npos);
  ASSERT_TRUE(
      normalized_cpp.find(
          R"(td_api::object_ptr<td_api::botAccessSettings>BotAccessSettings::get_bot_access_settings_object(Td*td)const{)") !=
      td::string::npos);
}

TEST(ManagedBotAccessSettingsContract, BotInfoManagerMustUseFailClosedAccessDispatchForGetAndSet) {
  auto normalized_access =
      td::managed_bot_access_settings_test::normalized_managed_bot_access_settings_access_h_source();
  auto normalized_get = td::managed_bot_access_settings_test::normalize_for_contract(
      td::managed_bot_access_settings_test::get_bot_access_settings_function_region());
  auto normalized_set = td::managed_bot_access_settings_test::normalize_for_contract(
      td::managed_bot_access_settings_test::set_bot_access_settings_function_region());

  const auto auth_guard =
      R"(if(!is_bot_session){reject_access(std::forward<PromiseT>(promise),Status::Error(400,"Onlybotscanusethemethod"));returnManagedBotAccessSettingsAccessResult::RejectedNonBotSession;})";
  const auto ownership_guard =
      R"(if(!bot_data.ok().can_be_edited){reject_access(std::forward<PromiseT>(promise),Status::Error(400,"Botmustbeowned"));returnManagedBotAccessSettingsAccessResult::RejectedUnownedBot;})";
  const auto read_delegate = R"(delegate_to_manager(managed_bot_user_id,std::forward<PromiseT>(promise));)";
  const auto write_delegate =
      R"(delegate_to_manager(managed_bot_user_id,std::forward<SettingsT>(settings),std::forward<PromiseT>(promise));)";
  const auto read_seam =
      "dispatch_managed_bot_access_settings_read(td_->auth_manager_->is_bot(),bot_user_id.get(),std::move(promise),";
  const auto write_seam =
      "dispatch_managed_bot_access_settings_write(td_->auth_manager_->is_bot(),bot_user_id.get(),std::move(access_"
      "settings),std::move(promise),";

  auto auth_guard_pos = normalized_access.find(auth_guard);
  auto ownership_guard_pos = normalized_access.find(ownership_guard);
  auto read_delegate_pos = normalized_access.find(read_delegate);
  auto write_delegate_pos = normalized_access.find(write_delegate);

  ASSERT_NE(td::string::npos, auth_guard_pos);
  ASSERT_NE(td::string::npos, ownership_guard_pos);
  ASSERT_NE(td::string::npos, read_delegate_pos);
  ASSERT_NE(td::string::npos, write_delegate_pos);
  ASSERT_NE(td::string::npos, normalized_get.find(read_seam));
  ASSERT_NE(td::string::npos, normalized_set.find(write_seam));
  ASSERT_TRUE(auth_guard_pos < ownership_guard_pos);
  ASSERT_TRUE(ownership_guard_pos < read_delegate_pos);
  ASSERT_TRUE(ownership_guard_pos < write_delegate_pos);
}
