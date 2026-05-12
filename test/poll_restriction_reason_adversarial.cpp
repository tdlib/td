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

TEST(PollRestrictionReasonAdversarial, PollManagerUsesDedicatedRestrictionReasonBuilderInPollObjectPath) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/PollManager.cpp");
  auto region = extract_region(
      source, "td_api::object_ptr<td_api::poll> PollManager::get_poll_object(PollId poll_id, const Poll *poll) const {",
      "PollId PollManager::create_poll(");

  ASSERT_TRUE(region.find("get_poll_vote_restriction_reason_object") != td::string::npos);
  ASSERT_TRUE(region.find("std::move(vote_restriction_reason)") != td::string::npos);
}

TEST(PollRestrictionReasonAdversarial, MessageContentRestrictionReasonBuilderReturnsNullWhenVotingAllowed) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessageContent.cpp");

  ASSERT_TRUE(
      source.find("td_api::object_ptr<td_api::PollVoteRestrictionReason> get_poll_vote_restriction_reason_object(") !=
      td::string::npos);
  ASSERT_TRUE(source.find("return nullptr;") != td::string::npos);
  ASSERT_EQ(source.find("pollVoteRestrictionReasonQuickReply"), td::string::npos);
}

}  // namespace
