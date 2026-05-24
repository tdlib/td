// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/managed_bot_token_access_test_utils.h"

TEST(ManagedBotTokenAccessAdversarial, LegacyEndpointMustNotBypassBotInfoManagerGuardPath) {
  auto normalized = td::managed_bot_token_access_test::normalize_for_contract(
      td::managed_bot_token_access_test::requests_get_bot_token_handler_region());

  ASSERT_NE(td::string::npos, normalized.find(R"(CHECK_IS_BOT();)"));
  ASSERT_NE(
      td::string::npos,
      normalized.find(R"(dispatch_get_managed_bot_token(request.bot_user_id_,request.revoke_,std::move(promise));)"));
  ASSERT_EQ(
      td::string::npos,
      normalized.find(
          R"(bot_info_manager_->get_bot_token(UserId(request.bot_user_id_),request.revoke_,std::move(promise));)"));
  ASSERT_EQ(td::string::npos, normalized.find(R"(create_handler<ExportBotTokenQuery>)"));
}

TEST(ManagedBotTokenAccessAdversarial, ManagedEndpointMustNotBypassBotInfoManagerGuardPath) {
  auto normalized = td::managed_bot_token_access_test::normalize_for_contract(
      td::managed_bot_token_access_test::requests_get_managed_bot_token_handler_region());

  ASSERT_NE(td::string::npos, normalized.find(R"(CHECK_IS_BOT();)"));
  ASSERT_NE(
      td::string::npos,
      normalized.find(R"(dispatch_get_managed_bot_token(request.bot_user_id_,request.revoke_,std::move(promise));)"));
  ASSERT_EQ(
      td::string::npos,
      normalized.find(
          R"(bot_info_manager_->get_bot_token(UserId(request.bot_user_id_),request.revoke_,std::move(promise));)"));
  ASSERT_EQ(td::string::npos, normalized.find(R"(create_handler<ExportBotTokenQuery>)"));
}

TEST(ManagedBotTokenAccessAdversarial, SharedDispatchHelperMustHaveSingleManagerDelegation) {
  auto normalized = td::managed_bot_token_access_test::normalize_for_contract(
      td::managed_bot_token_access_test::requests_dispatch_get_managed_bot_token_region());

  ASSERT_NE(
      td::string::npos,
      normalized.find(R"(bot_info_manager_->get_bot_token(managed_bot_user_id,managed_revoke,std::move(promise));)"));
  ASSERT_NE(td::string::npos, normalized.find(R"(dispatch_managed_bot_token_request()"));
  ASSERT_EQ(td::string::npos, normalized.find(R"(create_handler<ExportBotTokenQuery>)"));
}

TEST(ManagedBotTokenAccessAdversarial, SharedDispatchHelperMustFailClosedForNonBotSessions) {
  auto normalized = td::managed_bot_token_access_test::normalize_for_contract(
      td::managed_bot_token_access_test::requests_dispatch_get_managed_bot_token_region());

  const auto seam_call_prefix =
      R"(dispatch_managed_bot_token_request(td_->auth_manager_->is_bot(),bot_user_id,revoke,std::move(promise),)";
  const auto delegate_call =
      R"(bot_info_manager_->get_bot_token(managed_bot_user_id,managed_revoke,std::move(promise));)";
  const auto legacy_inline_auth_guard =
      R"(if(!td_->auth_manager_->is_bot()){returnpromise.set_error(Status::Error(400,"Onlybotscanusethemethod"));})";

  auto seam_call_pos = normalized.find(seam_call_prefix);
  auto delegate_call_pos = normalized.find(delegate_call);

  ASSERT_NE(td::string::npos, seam_call_pos);
  ASSERT_NE(td::string::npos, delegate_call_pos);
  ASSERT_EQ(td::string::npos, normalized.find(legacy_inline_auth_guard));
  ASSERT_TRUE(seam_call_pos < delegate_call_pos);
}

TEST(ManagedBotTokenAccessAdversarial, OwnershipGuardMustUseFailClosedErrorContract) {
  auto normalized_access = td::managed_bot_token_access_test::normalized_managed_bot_token_access_h_source();
  auto normalized_manager = td::managed_bot_token_access_test::normalize_for_contract(
      td::managed_bot_token_access_test::get_bot_token_function_region());

  ASSERT_NE(td::string::npos, normalized_access.find(R"(Status::Error(400,"Botmustbeowned"))"));
  ASSERT_NE(td::string::npos, normalized_access.find(R"(returnManagedBotTokenAccessResult::RejectedUnownedBot;)"));
  ASSERT_EQ(td::string::npos,
            normalized_manager.find(R"(returnpromise.set_error(Status::Error(400,"Botmustbeowned"));)"));
}
