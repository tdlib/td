// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "td/telegram/MessageContent.h"

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

TEST(PollRestrictionReasonContract, TdApiSchemaDeclaresVoteRestrictionReasonTaxonomy) {
  auto source = td::mtproto::test::read_repo_text_file("td/generate/scheme/td_api.tl");

  ASSERT_TRUE(source.find("//@class PollVoteRestrictionReason") != td::string::npos);
  ASSERT_TRUE(source.find("pollVoteRestrictionReasonMembershipRequired") != td::string::npos);
  ASSERT_TRUE(source.find("pollVoteRestrictionReasonOther = PollVoteRestrictionReason;") != td::string::npos);
  ASSERT_EQ(source.find("pollVoteRestrictionReasonQuickReply"), td::string::npos);
}

TEST(PollRestrictionReasonContract, PollObjectExposesVoteRestrictionReasonField) {
  auto source = td::mtproto::test::read_repo_text_file("td/generate/scheme/td_api.tl");

  ASSERT_TRUE(source.find("vote_restriction_reason:PollVoteRestrictionReason") != td::string::npos);
}

TEST(PollRestrictionReasonContract, RestrictionReasonHelperReturnsNullWhenVotingIsAllowed) {
  auto reason = td::get_poll_vote_restriction_reason_object(false);
  ASSERT_TRUE(reason == nullptr);
}

TEST(PollRestrictionReasonContract, RestrictionReasonHelperLegacyMembershipValueMapsToMembershipRequired) {
  auto reason = td::get_poll_vote_restriction_reason_object(1);
  ASSERT_TRUE(reason != nullptr);
  ASSERT_EQ(reason->get_id(), td::td_api::pollVoteRestrictionReasonMembershipRequired::ID);
}

TEST(PollRestrictionReasonContract, RestrictionReasonHelperUnknownLegacyValueFailsClosedToOther) {
  auto reason = td::get_poll_vote_restriction_reason_object(777);
  ASSERT_TRUE(reason != nullptr);
  ASSERT_EQ(reason->get_id(), td::td_api::pollVoteRestrictionReasonOther::ID);
}

TEST(PollRestrictionReasonContract, RestrictionReasonHelperReturnsMembershipRequiredWhenRestricted) {
  auto reason = td::get_poll_vote_restriction_reason_object(true);
  ASSERT_TRUE(reason != nullptr);
  ASSERT_EQ(reason->get_id(), td::td_api::pollVoteRestrictionReasonMembershipRequired::ID);
}

TEST(PollRestrictionReasonContract, PollObjectPathTagsSyntheticRestrictionReasonAsOtherFailClosed) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/PollManager.cpp");
  auto region = extract_region(
      source, "td_api::object_ptr<td_api::poll> PollManager::get_poll_object(PollId poll_id, const Poll *poll,",
      "PollId PollManager::create_poll(");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("int32vote_restriction_reason_tag=0;") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(!can_get_voters){") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(!is_real_message_content){vote_restriction_reason_tag=2;}") != td::string::npos);
  ASSERT_TRUE(normalized.find("elseif(poll->hide_results_until_close_){vote_restriction_reason_tag=1;}") !=
              td::string::npos);
  ASSERT_TRUE(
      normalized.find("autovote_restriction_reason=get_poll_vote_restriction_reason_object(vote_restriction_reason_"
                      "tag);") != td::string::npos);
}

}  // namespace
