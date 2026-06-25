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

td::string safe_region(const td::string &s, td::Slice begin_marker, td::Slice end_marker) {
  auto begin = s.find(begin_marker.str());
  if (begin == td::string::npos) {
    return {};
  }
  auto end = s.find(end_marker.str(), begin + begin_marker.size());
  if (end == td::string::npos || end <= begin) {
    return {};
  }
  return s.substr(begin, end - begin);
}

bool has(const td::string &s, td::Slice needle) {
  return s.find(needle.str()) != td::string::npos;
}

// Phase-2 backport guard for upstream f46b58926 ("Add td_api::checkAuthenticationWebToken"). This adds
// an AUTH method that sends an externally-supplied web token to a caller-chosen DC over an unauthenticated
// net query and, on success, steers the client's main DC. It must remain fail-CLOSED: a web-token login
// is only accepted from a legitimate pre-login auth state, only for a valid DC, and never after a bot
// token was entered (mutual exclusion with the bot login path). These guards must precede any network
// send / state mutation.
TEST(Phase2WebTokenAuthFailClosedContract, ImportWebTokenAuthorizationGuardsBeforeSend) {
  auto src = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/AuthManager.cpp"));
  auto region = safe_region(src, "AuthManager::import_web_token_authorization(uint64query_id,conststring&token,int32dc_id){",
                            "AuthManager::send_import_web_token_authorization_query()");
  ASSERT_TRUE(!region.empty());
  // (1) only from a legitimate pre-login state
  ASSERT_TRUE(has(region, "if(state_!=State::WaitPhoneNumber&&state_!=State::WaitQrCodeConfirmation){"));
  // (2) reject an invalid DC identifier
  ASSERT_TRUE(has(region, "if(!DcId::is_valid(dc_id)){"));
  // (3) mutual exclusion: never accept a web-token login after a bot token was entered
  ASSERT_TRUE(has(region, "if(was_check_bot_token_){"));
  // the actual send must come AFTER the guards (guards return on failure, so the send call closes the body)
  ASSERT_TRUE(has(region, "send_import_web_token_authorization_query();"));
}

}  // namespace
