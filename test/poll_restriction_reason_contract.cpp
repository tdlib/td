// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "td/telegram/MessageContent.h"

#include "test/stealth/SourceContractFileReader.h"

namespace {

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

}  // namespace
