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

// Phase-1 backport guard for upstream f73ea29cc ("Add and use get_min_message_senders_object").
// The conflict was resolved by adopting upstream's helper WHILE preserving the fork's W3-P
// fail-closed voter-visibility hardening. This pins that contract so the bulk backport cannot
// silently drop it: when the current user cannot get voters and is not a bot, recent_voters MUST be
// cleared (no voter leakage in get_poll_object).
TEST(Phase1PollVoterVisibilityContract, RecentVotersClearedWhenNotAllowed) {
  auto src = td::mtproto::test::read_repo_text_file("td/telegram/PollManager.cpp");
  auto region = extract_region(
      src, "get_min_message_senders_object(td_, poll->recent_voter_dialog_ids_, \"get_poll_object\")", "option_order");
  auto n = normalize_for_contract(region);
  ASSERT_TRUE(n.find("if(!can_get_voters&&!td_->auth_manager_->is_bot()){recent_voters.clear();}") != td::string::npos);
}

}  // namespace
