// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include "test/managed_bot_access_settings_test_utils.h"

#include <array>

namespace {

struct ContractNeedle {
  td::string haystack;
  td::string needle;
};

}  // namespace

TEST(ManagedBotAccessSettingsLightFuzz, ContractNeedlesMustRemainPresentAcrossDeterministicSampling) {
  const auto td_api_source = td::managed_bot_access_settings_test::normalized_td_api_source();
  const auto telegram_api_source = td::managed_bot_access_settings_test::normalized_telegram_api_source();
  const auto requests_h_source = td::managed_bot_access_settings_test::normalized_requests_h_source();
  const auto requests_cpp_source = td::managed_bot_access_settings_test::normalized_requests_cpp_source();
  const auto bot_info_source = td::managed_bot_access_settings_test::normalized_bot_info_manager_cpp_source();
  const auto bot_access_h_source = td::managed_bot_access_settings_test::normalized_bot_access_settings_h_source();
  const auto access_source =
      td::managed_bot_access_settings_test::normalized_managed_bot_access_settings_access_h_source();

  const std::array<ContractNeedle, 18> matrix = {{
      {td_api_source, R"(botAccessSettingsis_restricted:Booladded_user_ids:vector<int53>=BotAccessSettings;)"},
      {td_api_source, R"(getManagedBotAccessSettingsbot_user_id:int53=BotAccessSettings;)"},
      {td_api_source, R"(setManagedBotAccessSettingsbot_user_id:int53settings:botAccessSettings=Ok;)"},
      {telegram_api_source,
       R"(bots.accessSettings#dd1fbf93flags:#restricted:flags.0?trueadd_users:flags.1?Vector<User>=bots.AccessSettings;)"},
      {telegram_api_source, R"(bots.getAccessSettings#213853a3bot:InputUser=bots.AccessSettings;)"},
      {telegram_api_source,
       R"(bots.editAccessSettings#31813cd8flags:#restricted:flags.0?truebot:InputUseradd_users:flags.1?Vector<InputUser>=Bool;)"},
      {requests_h_source, R"(voidon_request(uint64id,consttd_api::getManagedBotAccessSettings&request);)"},
      {requests_h_source, R"(voidon_request(uint64id,td_api::setManagedBotAccessSettings&request);)"},
      {requests_cpp_source,
       R"(voidRequests::on_request(uint64id,consttd_api::getManagedBotAccessSettings&request){CHECK_IS_BOT();CREATE_REQUEST_PROMISE();td_->bot_info_manager_->get_bot_access_settings(UserId(request.bot_user_id_),std::move(promise));})"},
      {requests_cpp_source,
       R"(voidRequests::on_request(uint64id,td_api::setManagedBotAccessSettings&request){CHECK_IS_BOT();CREATE_OK_REQUEST_PROMISE();td_->bot_info_manager_->set_bot_access_settings(UserId(request.bot_user_id_),std::move(request.settings_),std::move(promise));})"},
      {bot_info_source,
       "dispatch_managed_bot_access_settings_read(td_->auth_manager_->is_bot(),bot_user_id.get(),std::move(promise),"},
      {bot_info_source,
       "dispatch_managed_bot_access_settings_write(td_->auth_manager_->is_bot(),bot_user_id.get(),std::move(access_"
       "settings),std::move(promise),"},
      {bot_info_source, R"(fetch_result<telegram_api::bots_getAccessSettings>(packet);)"},
      {bot_info_source, R"(fetch_result<telegram_api::bots_editAccessSettings>(packet);)"},
      {bot_access_h_source,
       R"(BotAccessSettings(Td*td,telegram_api::object_ptr<telegram_api::bots_accessSettings>&&settings);)"},
      {bot_access_h_source, R"(explicitBotAccessSettings(td_api::object_ptr<td_api::botAccessSettings>&&settings);)"},
      {access_source, R"(Status::Error(400,"Onlybotscanusethemethod"))"},
      {access_source, R"(Status::Error(400,"Botmustbeowned"))"},
  }};

  for (int iteration = 0; iteration < 10000; iteration++) {
    auto index = static_cast<size_t>(td::Random::fast(0, static_cast<int>(matrix.size()) - 1));
    const auto &contract = matrix[index];
    ASSERT_NE(td::string::npos, contract.haystack.find(contract.needle));
  }
}
