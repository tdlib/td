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
    auto byte = static_cast<unsigned char>(c);
    if (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

TEST(PollVoterVisibilityAdversarial, HiddenResultsNeverLeakRecentVotersThroughPollObject) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/PollManager.cpp");
  auto region = extract_region(
      source, "td_api::object_ptr<td_api::poll> PollManager::get_poll_object(PollId poll_id, const Poll *poll",
      "PollId PollManager::create_poll(");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("if(!can_get_voters&&!td_->auth_manager_->is_bot()){") != td::string::npos);
  ASSERT_TRUE(normalized.find("poll_option->recent_voter_ids_.clear();") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(!can_get_voters&&!td_->auth_manager_->is_bot()){recent_voters.clear();}") !=
              td::string::npos);
}

}  // namespace