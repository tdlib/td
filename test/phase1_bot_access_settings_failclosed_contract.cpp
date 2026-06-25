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

// Phase-1 backport guard for upstream b22850524 ("Return UserId from on_get_user").
// In this fork, the managed-bot access-settings server-payload path must remain fail-closed even when no
// live Td/UserManager context is available. That means the raw server user must be validated first, and only
// then normalized through on_get_user. This pins that ordering: an invalid added user MUST set an error,
// clear the accumulated user ids, and return (no partial/permissive bot-access settings, no null-td deref).
TEST(Phase1BotAccessSettingsFailClosedContract, InvalidAddedUserFailsClosed) {
  auto src = td::mtproto::test::read_repo_text_file("td/telegram/BotAccessSettings.cpp");
  auto region = extract_region(src, "if (is_restricted_) {", "added_user_ids_.push_back(user_id);");
  auto n = normalize_for_contract(region);
  ASSERT_TRUE(n.find("autouser_id=get_server_user_id(user);") != td::string::npos);
  // fail-closed on invalid user: error + clear + return
  ASSERT_TRUE(n.find("if(!user_id.is_valid()){") != td::string::npos);
  ASSERT_TRUE(n.find("added_user_ids_.clear();return;") != td::string::npos);
  // only valid users may reach normalization/storage
  ASSERT_TRUE(n.find("CHECK(td!=nullptr);") != td::string::npos);
  ASSERT_TRUE(n.find("td->user_manager_->on_get_user(std::move(user),\"BotAccessSettings\");") != td::string::npos);
}

}  // namespace
