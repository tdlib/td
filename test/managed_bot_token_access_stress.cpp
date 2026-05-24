// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/managed_bot_token_access_test_utils.h"

TEST(ManagedBotTokenAccessStress, RepeatedContractExtractionMustRemainStable) {
  constexpr int kIterations = 3000;

  for (int iteration = 0; iteration < kIterations; iteration++) {
    const auto normalized_td_api = td::managed_bot_token_access_test::normalized_td_api_source();
    const auto normalized_requests = td::managed_bot_token_access_test::normalized_requests_cpp_source();
    const auto normalized_bot_info = td::managed_bot_token_access_test::normalize_for_contract(
        td::managed_bot_token_access_test::get_bot_token_function_region());
    const auto normalized_access = td::managed_bot_token_access_test::normalized_managed_bot_token_access_h_source();

    auto managed_api_pos = normalized_td_api.find(R"(getManagedBotTokenbot_user_id:int53revoke:Bool=Text;)");
    auto legacy_api_pos = normalized_td_api.find(R"(getBotTokenbot_user_id:int53revoke:Bool=Text;)");
    auto shared_dispatch_pos = normalized_requests.find(
        R"(dispatch_get_managed_bot_token(request.bot_user_id_,request.revoke_,std::move(promise));)");
    auto manager_delegate_pos = normalized_requests.find(
        R"(bot_info_manager_->get_bot_token(managed_bot_user_id,managed_revoke,std::move(promise));)");
    auto seam_call_pos = normalized_requests.find(
        R"(dispatch_managed_bot_token_request(td_->auth_manager_->is_bot(),bot_user_id,revoke,std::move(promise),)");
    auto dispatch_auth_guard_pos = normalized_requests.find(
        R"(if(!td_->auth_manager_->is_bot()){returnpromise.set_error(Status::Error(400,"Onlybotscanusethemethod"));})");
    auto manager_seam_call_pos = normalized_bot_info.find(
        R"(autoresult=dispatch_managed_bot_token_export(td_->auth_manager_->is_bot(),bot_user_id.get(),revoke,std::move(promise),)");
    auto manager_auth_guard_pos = normalized_bot_info.find(
        R"(if(!td_->auth_manager_->is_bot()){returnpromise.set_error(Status::Error(400,"Onlybotscanusethemethod"));})");
    auto manager_ownership_guard_pos = normalized_bot_info.find(
        R"(if(!bot_data.can_be_edited){returnpromise.set_error(Status::Error(400,"Botmustbeowned"));})");
    auto export_pos = normalized_bot_info.find(
        R"(td_->create_handler<ExportBotTokenQuery>(std::move(managed_promise))->send(managed_bot_user_id,managed_revoke);)");
    auto access_auth_guard_pos = normalized_access.find(
        R"(if(!is_bot_session){reject_access(std::forward<PromiseT>(promise),Status::Error(400,"Onlybotscanusethemethod"));returnManagedBotTokenAccessResult::RejectedNonBotSession;})");
    auto access_lookup_error_pos = normalized_access.find(
        R"(if(bot_data.is_error()){reject_access(std::forward<PromiseT>(promise),bot_data.move_as_error());returnManagedBotTokenAccessResult::RejectedTargetLookupError;})");
    auto access_ownership_guard_pos = normalized_access.find(
        R"(if(!bot_data.ok().can_be_edited){reject_access(std::forward<PromiseT>(promise),Status::Error(400,"Botmustbeowned"));returnManagedBotTokenAccessResult::RejectedUnownedBot;})");
    auto access_delegate_pos =
        normalized_access.find(R"(delegate_to_exporter(managed_bot_user_id,revoke,std::forward<PromiseT>(promise));)");

    ASSERT_NE(td::string::npos, managed_api_pos);
    ASSERT_NE(td::string::npos, legacy_api_pos);
    ASSERT_NE(td::string::npos, shared_dispatch_pos);
    ASSERT_NE(td::string::npos, manager_delegate_pos);
    ASSERT_NE(td::string::npos, seam_call_pos);
    ASSERT_NE(td::string::npos, manager_seam_call_pos);
    ASSERT_NE(td::string::npos, export_pos);
    ASSERT_NE(td::string::npos, access_auth_guard_pos);
    ASSERT_NE(td::string::npos, access_lookup_error_pos);
    ASSERT_NE(td::string::npos, access_ownership_guard_pos);
    ASSERT_NE(td::string::npos, access_delegate_pos);
    ASSERT_EQ(td::string::npos, dispatch_auth_guard_pos);
    ASSERT_EQ(td::string::npos, manager_auth_guard_pos);
    ASSERT_EQ(td::string::npos, manager_ownership_guard_pos);
    ASSERT_TRUE(seam_call_pos < manager_delegate_pos);
    ASSERT_TRUE(manager_seam_call_pos < export_pos);
    ASSERT_TRUE(access_auth_guard_pos < access_lookup_error_pos);
    ASSERT_TRUE(access_lookup_error_pos < access_ownership_guard_pos);
    ASSERT_TRUE(access_ownership_guard_pos < access_delegate_pos);

    ASSERT_EQ(2u, td::managed_bot_token_access_test::count_substring(
                      normalized_requests,
                      R"(dispatch_get_managed_bot_token(request.bot_user_id_,request.revoke_,std::move(promise));)"));
    ASSERT_EQ(1u, td::managed_bot_token_access_test::count_substring(
                      normalized_requests,
                      R"(bot_info_manager_->get_bot_token(managed_bot_user_id,managed_revoke,std::move(promise));)"));
    ASSERT_EQ(
        1u,
        td::managed_bot_token_access_test::count_substring(
            normalized_bot_info,
            R"(dispatch_managed_bot_token_export(td_->auth_manager_->is_bot(),bot_user_id.get(),revoke,std::move(promise),)"));
  }
}
