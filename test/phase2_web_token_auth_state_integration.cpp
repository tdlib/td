// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

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

}  // namespace

TEST(Phase2WebTokenAuthStateIntegration, FailedWaitPhoneNumberWebTokenPathResetsBeforeReply) {
  auto source = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/AuthManager.cpp"));

  ASSERT_TRUE(source.find("voidAuthManager::reset_wait_phone_number_query_state(){") != td::string::npos);
  ASSERT_TRUE(source.find("web_token_={};") != td::string::npos);
  ASSERT_TRUE(source.find("web_token_dc_id_=0;") != td::string::npos);
  ASSERT_TRUE(source.find("was_web_token_login_request_=false;") != td::string::npos);
  ASSERT_TRUE(source.find("if(query_id_!=0){if(state_==State::WaitPhoneNumber){"
                          "reset_wait_phone_number_query_state();}"
                          "on_current_query_error(net_query->move_as_error());returntrue;}") != td::string::npos);
}

TEST(Phase2WebTokenAuthStateIntegration, ImportGuardAndResetUseTheSameTransientWebTokenFlag) {
  auto source = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/AuthManager.cpp"));

  ASSERT_TRUE(source.find("voidAuthManager::import_web_token_authorization(uint64query_id,conststring&token,"
                          "int32dc_id){") != td::string::npos);
  ASSERT_TRUE(source.find("was_web_token_login_request_=true;") != td::string::npos);
  ASSERT_TRUE(source.find("voidAuthManager::check_bot_token(uint64query_id,stringbot_token){") != td::string::npos);
  ASSERT_TRUE(source.find("was_web_token_login_request_){returnon_query_error(query_id,"
                          "Status::Error(400,\"Cannotsetbottokenafterauthenticationbegan."
                          "Youmustlogoutfirst\"));}") != td::string::npos);
  ASSERT_TRUE(source.find("was_web_token_login_request_=false;") != td::string::npos);
}

TEST(Phase2WebTokenAuthStateIntegration, FailedWebTokenPathClearsStoredDcBeforeLaterPasswordFallbackCanReuseIt) {
  auto source = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/AuthManager.cpp"));

  ASSERT_TRUE(source.find("if(query_id_!=0){if(state_==State::WaitPhoneNumber){"
                          "reset_wait_phone_number_query_state();}"
                          "on_current_query_error(net_query->move_as_error());returntrue;}") != td::string::npos);
  ASSERT_TRUE(source.find("voidAuthManager::reset_wait_phone_number_query_state(){other_user_ids_.clear();"
                          "send_code_helper_=SendCodeHelper();terms_of_service_=TermsOfService();"
                          "passkey_parameters_={};web_token_={};was_qr_code_request_=false;"
                          "was_passkey_login_request_=false;was_web_token_login_request_=false;"
                          "was_check_bot_token_=false;web_token_dc_id_=0;}") != td::string::npos);
  ASSERT_TRUE(source.find("}elseif(web_token_dc_id_!=0){G()->net_query_dispatcher().set_main_dc_id(web_token_dc_id_);"
                          "web_token_dc_id_=0;}") != td::string::npos);
}
