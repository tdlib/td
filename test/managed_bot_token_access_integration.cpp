// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/managed_bot_token_access_test_utils.h"

TEST(ManagedBotTokenAccessIntegration, BothRequestsHandlersMustDelegateToSameManagerMethod) {
  auto requests_source = td::managed_bot_token_access_test::normalized_requests_cpp_source();

  ASSERT_EQ(1u, td::managed_bot_token_access_test::count_substring(
                    requests_source, R"(#include"td/telegram/ManagedBotTokenDispatch.h")"));

  ASSERT_EQ(2u, td::managed_bot_token_access_test::count_substring(
                    requests_source,
                    R"(dispatch_get_managed_bot_token(request.bot_user_id_,request.revoke_,std::move(promise));)"));

  ASSERT_EQ(1u, td::managed_bot_token_access_test::count_substring(
                    requests_source,
                    R"(bot_info_manager_->get_bot_token(managed_bot_user_id,managed_revoke,std::move(promise));)"));
  ASSERT_EQ(1u, td::managed_bot_token_access_test::count_substring(requests_source,
                                                                   R"(dispatch_managed_bot_token_request()"));
}

TEST(ManagedBotTokenAccessIntegration, CliMustExposeBothLegacyAndManagedTokenCommands) {
  auto normalized_cli = td::managed_bot_token_access_test::normalized_cli_source();

  ASSERT_NE(td::string::npos, normalized_cli.find(R"(op=="gbt"||op=="gbtr")"));
  ASSERT_NE(td::string::npos, normalized_cli.find(R"(op=="gmbt"||op=="gmbtr")"));
  ASSERT_NE(td::string::npos,
            normalized_cli.find(R"(send_request(td_api::make_object<td_api::getBotToken>(bot_user_id,op=="gbtr"));)"));
  ASSERT_NE(td::string::npos,
            normalized_cli.find(
                R"(send_request(td_api::make_object<td_api::getManagedBotToken>(bot_user_id,op=="gmbtr"));)"));
}
