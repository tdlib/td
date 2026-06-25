// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <string_view>

namespace {

td::string extract_region(std::string_view source, td::Slice begin_marker, td::Slice end_marker) {
  auto begin = source.find(begin_marker.str());
  CHECK(begin != td::string::npos);
  auto end = source.find(end_marker.str(), begin + begin_marker.size());
  CHECK(end != td::string::npos);
  CHECK(end > begin);
  return td::string(source.substr(begin, end - begin));
}

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

// Phase-1 backport guard for upstream 4ae93d66b ("Add and use RestrictedRights::restrict_all()").
// The fork's RestrictedRights has 18 rights vs upstream's 19; the conflict was resolved by adopting
// restrict_all() at all-restricted construction sites. This pins the SECURITY-CRITICAL fail-closed
// contract that a buggy resolution previously broke (empty guard -> permissive fall-through, flagged
// HIGH by automated security review):
//   get_user_default_permissions MUST return restrict_all() for unknown users and the
//   Replies/VerificationCodes bots (NOT a permissive RestrictedRights, NOT an empty guard).
TEST(Phase1RestrictedRightsFailClosedContract, UnknownAndServiceBotsGetRestrictAll) {
  auto user_src = td::mtproto::test::read_repo_text_file("td/telegram/UserManager.cpp");
  auto guard = extract_region(user_src, "RestrictedRights UserManager::get_user_default_permissions(UserId user_id)",
                              "RestrictedRights UserManager::get_secret_chat_default_permissions");
  auto g = normalize_for_contract(guard);
  // the special-user guard exists and returns the fully-restricted set (fail-closed)
  ASSERT_TRUE(g.find("user_id==get_verification_codes_bot_user_id()){returnRestrictedRights::restrict_all();}") !=
              td::string::npos);
  // and is NOT an empty block (the regression form)
  ASSERT_TRUE(g.find("get_verification_codes_bot_user_id()){}") == td::string::npos);

  // ChatManager unknown-chat default permissions are also fully restricted (fail-closed default).
  auto chat_src = td::mtproto::test::read_repo_text_file("td/telegram/ChatManager.cpp");
  auto cn = normalize_for_contract(chat_src);
  ASSERT_TRUE(cn.find("get_chat_default_permissions(ChatIdchat_id)const{autoc=get_chat(chat_id);if(c==nullptr)"
                      "{returnRestrictedRights::restrict_all();}") != td::string::npos);
}

}  // namespace
