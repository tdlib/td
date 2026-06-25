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

TEST(Phase2WebTokenAuthStateContract, ImportPathMarksTheWebTokenFlowAsActive) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/AuthManager.cpp");
  auto region = normalize_for_contract(extract_region(
      source, "void AuthManager::import_web_token_authorization(uint64 query_id, const string &token, int32 dc_id) {",
      "void AuthManager::send_import_web_token_authorization_query()"));

  ASSERT_TRUE(region.find("web_token_=token;") != td::string::npos);
  ASSERT_TRUE(region.find("web_token_dc_id_=dc_id;") != td::string::npos);
  ASSERT_TRUE(region.find("was_web_token_login_request_=true;") != td::string::npos);
  ASSERT_TRUE(region.find("was_web_token_login_request_=false;") == td::string::npos);
}

TEST(Phase2WebTokenAuthStateContract, BotTokenGuardIncludesWebTokenLoginFlow) {
  auto source = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/AuthManager.cpp"));
  ASSERT_TRUE(source.find("if(!send_code_helper_.get_phone_number().empty()||was_qr_code_request_||"
                          "was_passkey_login_request_||was_web_token_login_request_){") != td::string::npos);
}

TEST(Phase2WebTokenAuthStateContract, WaitPhoneNumberResetClearsWebTokenLoginFlowState) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/AuthManager.cpp");
  auto region = normalize_for_contract(extract_region(source, "void AuthManager::reset_wait_phone_number_query_state() {",
                                                      "bool AuthManager::should_request_password_on_error("));

  ASSERT_TRUE(region.find("was_web_token_login_request_=false;") != td::string::npos);
}

TEST(Phase2WebTokenAuthStateContract, WaitPhoneNumberResetClearsStoredWebTokenRoutingState) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/AuthManager.cpp");
  auto region = normalize_for_contract(extract_region(source, "void AuthManager::reset_wait_phone_number_query_state() {",
                                                      "bool AuthManager::should_request_password_on_error("));

  ASSERT_TRUE(region.find("web_token_={};") != td::string::npos);
  ASSERT_TRUE(region.find("web_token_dc_id_=0;") != td::string::npos);
}

}  // namespace
