// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/managed_bot_token_access_test_utils.h"

TEST(ManagedBotTokenAccessContract, TdApiMustExposeManagedAndLegacyManagedBotTokenEndpoints) {
  auto normalized = td::managed_bot_token_access_test::normalized_td_api_source();

  ASSERT_TRUE(normalized.find(R"(getManagedBotTokenbot_user_id:int53revoke:Bool=Text;)") != td::string::npos);
  ASSERT_TRUE(normalized.find(R"(getBotTokenbot_user_id:int53revoke:Bool=Text;)") != td::string::npos);
}

TEST(ManagedBotTokenAccessContract, RequestsHeaderMustDeclareBothManagedTokenHandlers) {
  auto normalized = td::managed_bot_token_access_test::normalized_requests_h_source();

  ASSERT_TRUE(normalized.find(R"(voidon_request(uint64id,consttd_api::getBotToken&request);)") != td::string::npos);
  ASSERT_TRUE(normalized.find(R"(voidon_request(uint64id,consttd_api::getManagedBotToken&request);)") !=
              td::string::npos);
  ASSERT_TRUE(
      normalized.find(R"(voiddispatch_get_managed_bot_token(int64bot_user_id,boolrevoke,Promise<string>&&promise);)") !=
      td::string::npos);
}

TEST(ManagedBotTokenAccessContract, BotInfoManagerMustRejectUnownedBotsBeforeTokenExport) {
  auto normalized_access = td::managed_bot_token_access_test::normalized_managed_bot_token_access_h_source();
  auto normalized_manager = td::managed_bot_token_access_test::normalize_for_contract(
      td::managed_bot_token_access_test::get_bot_token_function_region());

  const auto guard =
      R"(if(!bot_data.ok().can_be_edited){reject_access(std::forward<PromiseT>(promise),Status::Error(400,"Botmustbeowned"));returnManagedBotTokenAccessResult::RejectedUnownedBot;})";
  const auto delegate_call = R"(delegate_to_exporter(managed_bot_user_id,revoke,std::forward<PromiseT>(promise));)";
  const auto seam_call =
      R"(autoresult=dispatch_managed_bot_token_export(td_->auth_manager_->is_bot(),bot_user_id.get(),revoke,std::move(promise),)";

  auto guard_pos = normalized_access.find(guard);
  auto delegate_pos = normalized_access.find(delegate_call);
  auto seam_call_pos = normalized_manager.find(seam_call);

  ASSERT_NE(td::string::npos, guard_pos);
  ASSERT_NE(td::string::npos, delegate_pos);
  ASSERT_NE(td::string::npos, seam_call_pos);
  ASSERT_TRUE(guard_pos < delegate_pos);
}

TEST(ManagedBotTokenAccessContract, BotInfoManagerMustFailClosedForNonBotSessions) {
  auto normalized_access = td::managed_bot_token_access_test::normalized_managed_bot_token_access_h_source();
  auto normalized_manager = td::managed_bot_token_access_test::normalize_for_contract(
      td::managed_bot_token_access_test::get_bot_token_function_region());

  const auto auth_guard =
      R"(if(!is_bot_session){reject_access(std::forward<PromiseT>(promise),Status::Error(400,"Onlybotscanusethemethod"));returnManagedBotTokenAccessResult::RejectedNonBotSession;})";
  const auto seam_call =
      R"(autoresult=dispatch_managed_bot_token_export(td_->auth_manager_->is_bot(),bot_user_id.get(),revoke,std::move(promise),)";
  const auto legacy_manager_auth_guard =
      R"(if(!td_->auth_manager_->is_bot()){returnpromise.set_error(Status::Error(400,"Onlybotscanusethemethod"));})";
  const auto ownership_guard =
      R"(if(!bot_data.ok().can_be_edited){reject_access(std::forward<PromiseT>(promise),Status::Error(400,"Botmustbeowned"));returnManagedBotTokenAccessResult::RejectedUnownedBot;})";

  auto auth_guard_pos = normalized_access.find(auth_guard);
  auto ownership_guard_pos = normalized_access.find(ownership_guard);
  auto seam_call_pos = normalized_manager.find(seam_call);

  ASSERT_NE(td::string::npos, auth_guard_pos);
  ASSERT_NE(td::string::npos, ownership_guard_pos);
  ASSERT_NE(td::string::npos, seam_call_pos);
  ASSERT_EQ(td::string::npos, normalized_manager.find(legacy_manager_auth_guard));
  ASSERT_TRUE(auth_guard_pos < ownership_guard_pos);
}
