// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

namespace {

td::string normalize_no_space(td::Slice source) {
  td::string out;
  out.reserve(source.size());
  for (auto c : source) {
    unsigned char byte = static_cast<unsigned char>(c);
    if (byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r') {
      continue;
    }
    out.push_back(c);
  }
  return out;
}

td::string auth_manager_source() {
  return normalize_no_space(td::mtproto::test::read_repo_text_file("td/telegram/AuthManager.cpp"));
}

}  // namespace

TEST(AuthManagerResultSourceContract, SessionPasswordFallbackUsesReviewedAllowlist) {
  auto source = auth_manager_source();

  ASSERT_TRUE(source.find("boolAuthManager::should_request_password_on_error(NetQueryTypetype,constStatus&error){") !=
              td::string::npos);
  ASSERT_TRUE(source.find("if(error.code()!=401||error.message()!=CSlice(\"SESSION_PASSWORD_NEEDED\"))"
                          "{returnfalse;}") != td::string::npos);
  ASSERT_TRUE(source.find("switch(type){caseSendCode:caseSendEmailCode:caseVerifyEmailAddress:caseSignIn:"
                          "caseRequestQrCode:caseImportQrCode:caseFinishPasskeyLogin:"
                          "caseImportWebTokenAuthorization:returntrue;"
                          "default:returnfalse;}") != td::string::npos);
}

TEST(AuthManagerResultSourceContract, ImportQrPasswordFallbackPinsImportedDcRouting) {
  auto source = auth_manager_source();

  ASSERT_TRUE(source.find("voidAuthManager::start_get_password_query(NetQueryTypetype,NetQueryPtr&net_query){") !=
              td::string::npos);
  ASSERT_TRUE(source.find("if(type==ImportQrCode){CHECK(DcId::is_valid(imported_dc_id_));"
                          "dc_id=DcId::internal(imported_dc_id_);}") != td::string::npos);
  ASSERT_TRUE(source.find("start_net_query(GetPassword,G()->net_query_creator().create_unauth("
                          "telegram_api::account_getPassword(),dc_id));") != td::string::npos);
}

TEST(AuthManagerResultSourceContract, WaitPhoneNumberErrorResetClearsTransientStateBeforeReply) {
  auto source = auth_manager_source();

  ASSERT_TRUE(source.find("voidAuthManager::reset_wait_phone_number_query_state(){other_user_ids_.clear();"
                          "send_code_helper_=SendCodeHelper();terms_of_service_=TermsOfService();"
                          "passkey_parameters_={};web_token_={};was_qr_code_request_=false;"
                          "was_passkey_login_request_=false;was_web_token_login_request_=false;"
                          "was_check_bot_token_=false;web_token_dc_id_=0;}") != td::string::npos);
  ASSERT_TRUE(source.find("if(query_id_!=0){if(state_==State::WaitPhoneNumber){"
                          "reset_wait_phone_number_query_state();}"
                          "on_current_query_error(net_query->move_as_error());returntrue;}") != td::string::npos);
}

TEST(AuthManagerResultSourceContract, BackgroundErrorIgnoreAllowlistStaysFailClosed) {
  auto source = auth_manager_source();

  ASSERT_TRUE(source.find("boolAuthManager::should_ignore_background_error(NetQueryTypetype){") != td::string::npos);
  ASSERT_TRUE(source.find("switch(type){caseRequestQrCode:caseImportQrCode:caseGetPassword:"
                          "caseFinishPasskeyLogin:caseImportWebTokenAuthorization:returnfalse;default:returntrue;}") !=
              td::string::npos);
  ASSERT_TRUE(source.find("LOG(INFO)<<\"Ignoreerrorfornetqueryoftype\"<<") != td::string::npos);
  ASSERT_TRUE(source.find("type=None;") != td::string::npos);
}
