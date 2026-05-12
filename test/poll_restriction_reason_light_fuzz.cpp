// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <array>

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

struct Probe {
  size_t source_index;
  td::Slice snippet;
};

std::array<td::string, 3> load_normalized_sources() {
  const std::array<td::Slice, 3> source_paths = {
      "td/telegram/MessageContent.cpp",
      "td/telegram/PollManager.cpp",
      "td/generate/scheme/td_api.tl",
  };

  std::array<td::string, source_paths.size()> normalized_sources;
  for (size_t i = 0; i < source_paths.size(); ++i) {
    normalized_sources[i] = normalize_for_contract(td::mtproto::test::read_repo_text_file(source_paths[i]));
  }
  return normalized_sources;
}

}  // namespace

TEST(PollRestrictionReasonLightFuzz, RandomizedProbeOrderPinsRestrictionReasonContracts) {
  auto normalized_sources = load_normalized_sources();

  const std::array<Probe, 15> required = {
      Probe{0,
            "td_api::object_ptr<td_api::PollVoteRestrictionReason>get_poll_vote_restriction_reason_object(int32"
            "legacy_reason_tag){"},
      Probe{0, "switch(legacy_reason_tag){"},
      Probe{0, "case0:returnnullptr;"},
      Probe{0, "case1:returntd_api::make_object<td_api::pollVoteRestrictionReasonMembershipRequired>();"},
      Probe{0, "default:returntd_api::make_object<td_api::pollVoteRestrictionReasonOther>();"},
      Probe{0,
            "td_api::object_ptr<td_api::PollVoteRestrictionReason>get_poll_vote_restriction_reason_object(bool"
            "is_membership_required){"},
      Probe{0, "returnget_poll_vote_restriction_reason_object(is_membership_required?1:0);"},
      Probe{1, "autovote_restriction_reason=get_poll_vote_restriction_reason_object("},
      Probe{1, "hide_results_until_close_&&!can_get_voters"},
      Probe{1, "std::move(vote_restriction_reason)"},
      Probe{2, "//@classPollVoteRestrictionReason"},
      Probe{2, "pollVoteRestrictionReasonMembershipRequired=PollVoteRestrictionReason;"},
      Probe{2, "pollVoteRestrictionReasonOther=PollVoteRestrictionReason;"},
      Probe{2, "vote_restriction_reason:PollVoteRestrictionReason"},
  };

  for (int i = 0; i < 12000; ++i) {
    auto idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(required.size()) - 1));
    const auto &probe = required[idx];
    ASSERT_NE(td::string::npos, normalized_sources[probe.source_index].find(probe.snippet.str()));
  }
}

TEST(PollRestrictionReasonLightFuzz, RandomizedProbeOrderPinsLegacyRestrictionReasonPatternsAbsent) {
  auto normalized_sources = load_normalized_sources();

  const std::array<Probe, 2> forbidden = {
      Probe{0, "pollVoteRestrictionReasonQuickReply"},
      Probe{2, "pollVoteRestrictionReasonQuickReply"},
  };

  for (int i = 0; i < 12000; ++i) {
    auto idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(forbidden.size()) - 1));
    const auto &probe = forbidden[idx];
    ASSERT_EQ(td::string::npos, normalized_sources[probe.source_index].find(probe.snippet.str()));
  }
}
