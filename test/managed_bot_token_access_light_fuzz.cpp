// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include "test/managed_bot_token_access_test_utils.h"

#include <array>

namespace {

struct ContractNeedle {
  td::string haystack;
  td::string needle;
};

}  // namespace

TEST(ManagedBotTokenAccessLightFuzz, ContractNeedlesMustRemainPresentAcrossDeterministicSampling) {
  const auto td_api_source = td::managed_bot_token_access_test::normalized_td_api_source();
  const auto requests_h_source = td::managed_bot_token_access_test::normalized_requests_h_source();
  const auto requests_cpp_source = td::managed_bot_token_access_test::normalized_requests_cpp_source();
  const auto bot_info_source = td::managed_bot_token_access_test::normalized_bot_info_manager_cpp_source();
  const auto access_source = td::managed_bot_token_access_test::normalized_managed_bot_token_access_h_source();

  const std::array<ContractNeedle, 13> matrix = {{
      {td_api_source, R"(getManagedBotTokenbot_user_id:int53revoke:Bool=Text;)"},
      {td_api_source, R"(getBotTokenbot_user_id:int53revoke:Bool=Text;)"},
      {requests_h_source, R"(voidon_request(uint64id,consttd_api::getManagedBotToken&request);)"},
      {requests_h_source,
       R"(voiddispatch_get_managed_bot_token(int64bot_user_id,boolrevoke,Promise<string>&&promise);)"},
      {requests_cpp_source,
       R"(voidRequests::on_request(uint64id,consttd_api::getManagedBotToken&request){CHECK_IS_BOT();CREATE_TEXT_REQUEST_PROMISE();dispatch_get_managed_bot_token(request.bot_user_id_,request.revoke_,std::move(promise));})"},
      {requests_cpp_source,
       R"(dispatch_managed_bot_token_request(td_->auth_manager_->is_bot(),bot_user_id,revoke,std::move(promise),)"},
      {bot_info_source,
       R"(autoresult=dispatch_managed_bot_token_export(td_->auth_manager_->is_bot(),bot_user_id.get(),revoke,std::move(promise),)"},
      {bot_info_source,
       R"(td_->create_handler<ExportBotTokenQuery>(std::move(managed_promise))->send(managed_bot_user_id,managed_revoke);)"},
      {access_source,
       R"(enumclassManagedBotTokenAccessResult:std::uint8_t{RejectedNonBotSession,RejectedTargetLookupError,RejectedUnownedBot,DelegatedToExporter,})"},
      {access_source, R"(Status::Error(400,"Onlybotscanusethemethod"))"},
      {access_source, R"(Status::Error(400,"Botmustbeowned"))"},
      {access_source, R"(returnManagedBotTokenAccessResult::RejectedTargetLookupError;)"},
      {requests_cpp_source,
       R"(voidRequests::dispatch_get_managed_bot_token(int64bot_user_id,boolrevoke,Promise<string>&&promise){autoresult=dispatch_managed_bot_token_request(td_->auth_manager_->is_bot(),bot_user_id,revoke,std::move(promise),[](Promise<string>&&promise,Statuserror)mutable{promise.set_error(std::move(error));},[this](UserIdmanaged_bot_user_id,boolmanaged_revoke,Promise<string>&&promise)mutable{td_->bot_info_manager_->get_bot_token(managed_bot_user_id,managed_revoke,std::move(promise));});static_cast<void>(result);})"},
  }};

  for (int iteration = 0; iteration < 10000; iteration++) {
    auto index = static_cast<size_t>(td::Random::fast(0, static_cast<int>(matrix.size()) - 1));
    const auto &contract = matrix[index];
    ASSERT_NE(td::string::npos, contract.haystack.find(contract.needle));
  }
}
