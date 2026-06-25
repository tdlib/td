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

TEST(Phase2WebTokenPasswordFallbackIntegration, WebTokenTwoFactorFallbackUsesSameDcAcrossImportAndPasswordFetch) {
  auto source = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/AuthManager.cpp"));

  ASSERT_TRUE(source.find("voidAuthManager::import_web_token_authorization(uint64query_id,conststring&token,"
                          "int32dc_id){") != td::string::npos);
  ASSERT_TRUE(source.find("web_token_dc_id_=dc_id;") != td::string::npos);
  ASSERT_TRUE(source.find("caseImportWebTokenAuthorization:returntrue;") != td::string::npos);
  ASSERT_TRUE(source.find("if(type==ImportWebTokenAuthorization){CHECK(DcId::is_valid(web_token_dc_id_));"
                          "dc_id=DcId::internal(web_token_dc_id_);}") != td::string::npos);
}

TEST(Phase2WebTokenPasswordFallbackIntegration, WebTokenTwoFactorFallbackPromotesChosenDcBeforePasswordCheck) {
  auto source = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/AuthManager.cpp"));

  ASSERT_TRUE(source.find("if(web_token_dc_id_!=0){G()->net_query_dispatcher().set_main_dc_id(web_token_dc_id_);"
                          "web_token_dc_id_=0;}") != td::string::npos);
  ASSERT_TRUE(source.find("start_net_query(NetQueryType::CheckPassword,") != td::string::npos);
}
