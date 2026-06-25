// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <string_view>

namespace {

td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    if (auto byte = static_cast<unsigned char>(c); byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

td::string extract_region(std::string_view source, td::Slice begin_marker, td::Slice end_marker) {
  auto begin = source.find(begin_marker.str());
  CHECK(begin != td::string::npos);
  auto end = source.find(end_marker.str(), begin + begin_marker.size());
  CHECK(end != td::string::npos);
  CHECK(end > begin);
  return td::string(source.substr(begin, end - begin));
}

TEST(Phase2WebTokenPasswordFallbackAdversarial, WebTokenTwoFactorMustNotSilentlyReuseMainDcPasswordFetch) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/AuthManager.cpp");
  auto region = normalize_for_contract(
      extract_region(source, "void AuthManager::start_get_password_query(NetQueryType type, NetQueryPtr &net_query) {",
                     "bool AuthManager::should_ignore_background_error("));

  ASSERT_TRUE(region.find("if(type==ImportWebTokenAuthorization){CHECK(DcId::is_valid(web_token_dc_id_));"
                          "dc_id=DcId::internal(web_token_dc_id_);}") != td::string::npos);
  ASSERT_TRUE(region.find("start_net_query(GetPassword,G()->net_query_creator().create_unauth("
                          "telegram_api::account_getPassword(),dc_id));") != td::string::npos);
}

TEST(Phase2WebTokenPasswordFallbackAdversarial, WebTokenTwoFactorMustNotLoseChosenDcBeforeCheckPassword) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/AuthManager.cpp");
  auto region = normalize_for_contract(
      extract_region(source, "void AuthManager::on_get_password_result(NetQueryPtr &&net_query) {",
                     "void AuthManager::on_request_password_recovery_result("));

  ASSERT_TRUE(region.find("if(web_token_dc_id_!=0){G()->net_query_dispatcher().set_main_dc_id(web_token_dc_id_);"
                          "web_token_dc_id_=0;}") != td::string::npos);
  ASSERT_TRUE(region.find("start_net_query(NetQueryType::CheckPassword,") != td::string::npos);
}

}  // namespace
