// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
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

TEST(PollRestrictionReasonStress, RepeatedSourceReadsKeepRestrictionReasonContractsStable) {
  constexpr int kIterations = 2200;
  td::uint32 checksum = 0;

  for (int i = 0; i < kIterations; ++i) {
    auto message_content_source =
        normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/MessageContent.cpp"));
    auto poll_manager_source =
        normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/PollManager.cpp"));
    auto td_api_source = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/generate/scheme/td_api.tl"));

    ASSERT_NE(td::string::npos,
          message_content_source.find("td_api::object_ptr<td_api::PollVoteRestrictionReason>"
                        "get_poll_vote_restriction_reason_object(int32legacy_reason_tag){"));
    ASSERT_NE(td::string::npos, message_content_source.find("switch(legacy_reason_tag){"));
    ASSERT_NE(td::string::npos, message_content_source.find("case0:returnnullptr;"));
    ASSERT_NE(td::string::npos,
          message_content_source.find("case1:returntd_api::make_object<td_api::"
                        "pollVoteRestrictionReasonMembershipRequired>();"));
    ASSERT_NE(td::string::npos,
          message_content_source.find("default:returntd_api::make_object<td_api::"
                        "pollVoteRestrictionReasonOther>();"));
    ASSERT_NE(td::string::npos,
          message_content_source.find("td_api::object_ptr<td_api::PollVoteRestrictionReason>"
                        "get_poll_vote_restriction_reason_object(boolis_membership_required){"));
    ASSERT_NE(td::string::npos,
          message_content_source.find("returnget_poll_vote_restriction_reason_object(is_membership_required?1:0);"));

    ASSERT_NE(td::string::npos,
              poll_manager_source.find("autovote_restriction_reason=get_poll_vote_restriction_reason_object("));
    ASSERT_NE(td::string::npos, poll_manager_source.find("hide_results_until_close_&&!can_get_voters"));
    ASSERT_NE(td::string::npos, poll_manager_source.find("std::move(vote_restriction_reason)"));

    ASSERT_NE(td::string::npos, td_api_source.find("//@classPollVoteRestrictionReason"));
    ASSERT_NE(td::string::npos,
              td_api_source.find("pollVoteRestrictionReasonMembershipRequired=PollVoteRestrictionReason;"));
    ASSERT_NE(td::string::npos, td_api_source.find("pollVoteRestrictionReasonOther=PollVoteRestrictionReason;"));
    ASSERT_NE(td::string::npos, td_api_source.find("vote_restriction_reason:PollVoteRestrictionReason"));

    ASSERT_EQ(td::string::npos, message_content_source.find("pollVoteRestrictionReasonQuickReply"));
    ASSERT_EQ(td::string::npos, td_api_source.find("pollVoteRestrictionReasonQuickReply"));

    auto helper_pos = message_content_source.find(
      "td_api::object_ptr<td_api::PollVoteRestrictionReason>"
      "get_poll_vote_restriction_reason_object(int32legacy_reason_tag){");
    auto switch_pos = message_content_source.find("switch(legacy_reason_tag){", helper_pos);
    auto none_return_pos = message_content_source.find("case0:returnnullptr;", switch_pos);
    auto membership_return_pos = message_content_source.find(
      "case1:returntd_api::make_object<td_api::pollVoteRestrictionReasonMembershipRequired>();", switch_pos);
    auto default_return_pos = message_content_source.find(
      "default:returntd_api::make_object<td_api::pollVoteRestrictionReasonOther>();", switch_pos);
    auto bool_overload_pos = message_content_source.find(
      "td_api::object_ptr<td_api::PollVoteRestrictionReason>"
      "get_poll_vote_restriction_reason_object(boolis_membership_required){",
      default_return_pos == td::string::npos ? 0 : default_return_pos);
    auto bool_delegate_pos = message_content_source.find(
      "returnget_poll_vote_restriction_reason_object(is_membership_required?1:0);",
      bool_overload_pos == td::string::npos ? 0 : bool_overload_pos);

    ASSERT_TRUE(helper_pos != td::string::npos);
    ASSERT_TRUE(switch_pos != td::string::npos);
    ASSERT_TRUE(none_return_pos != td::string::npos);
    ASSERT_TRUE(membership_return_pos != td::string::npos);
    ASSERT_TRUE(default_return_pos != td::string::npos);
    ASSERT_TRUE(bool_overload_pos != td::string::npos);
    ASSERT_TRUE(bool_delegate_pos != td::string::npos);
    ASSERT_TRUE(helper_pos < switch_pos);
    ASSERT_TRUE(switch_pos < none_return_pos);
    ASSERT_TRUE(none_return_pos < membership_return_pos);
    ASSERT_TRUE(membership_return_pos < default_return_pos);
    ASSERT_TRUE(default_return_pos < bool_overload_pos);
    ASSERT_TRUE(bool_overload_pos < bool_delegate_pos);

    checksum += static_cast<td::uint32>(message_content_source.size() + poll_manager_source.size() +
                      td_api_source.size() + static_cast<size_t>(i) + helper_pos + switch_pos +
                      none_return_pos + membership_return_pos + default_return_pos +
                      bool_overload_pos + bool_delegate_pos);
  }

  ASSERT_TRUE(checksum != 0);
}
