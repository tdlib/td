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

std::array<td::string, 2> load_normalized_sources() {
  const std::array<td::Slice, 2> source_paths = {
      "td/telegram/MessagesManager.cpp",
      "td/generate/scheme/td_api.tl",
  };

  std::array<td::string, source_paths.size()> normalized_sources;
  for (size_t i = 0; i < source_paths.size(); ++i) {
    normalized_sources[i] = normalize_for_contract(td::mtproto::test::read_repo_text_file(source_paths[i]));
  }
  return normalized_sources;
}

}  // namespace

TEST(PollUnreadVotesLightFuzz, RandomizedProbeOrderPinsUnreadPollVoteStateGuardPatterns) {
  auto normalized_sources = load_normalized_sources();

  const std::array<Probe, 13> required = {
      Probe{0, "returnm->contains_unread_poll_votes;"},
      Probe{0, "constboolhas_current_unread_votes=has_unread_poll_votes(d->dialog_id,m);"},
      Probe{0, "if(has_current_unread_votes==has_unread_votes){return;}"},
      Probe{0, "remove_message_unread_poll_votes(d,m,\"on_update_poll_has_unread_votes\");"},
      Probe{0, "m->contains_unread_poll_votes=true;"},
      Probe{0, "m->contains_unread_poll_votes=false;"},
      Probe{0, "remove_message_content_poll_has_unread_votes(td_,m->content.get());"},
      Probe{0, "send_update_message_contains_unread_poll_votes(d->dialog_id,m,d->unread_poll_vote_count);"},
      Probe{0, "autois_update_sent=read_all_local_dialog_poll_votes(dialog_id,forum_topic_id);"},
      Probe{0, "if(!is_update_sent){send_update_chat_unread_poll_vote_count(d,\"read_all_dialog_poll_votes\");}"},
      Probe{0, "on_dialog_updated(dialog_id,\"read_all_dialog_poll_votes\");"},
      Probe{1, "contains_unread_poll_votes:Bool"},
      Probe{1,
            "updateMessageContainsUnreadPollVoteschat_id:int53message_id:int53contains_unread_poll_votes:Bool"
            "unread_poll_vote_count:int32=Update;"},
  };

  for (int i = 0; i < 12000; ++i) {
    auto idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(required.size()) - 1));
    const auto &probe = required[idx];
    ASSERT_NE(td::string::npos, normalized_sources[probe.source_index].find(probe.snippet.str()));
  }
}

TEST(PollUnreadVotesLightFuzz, RandomizedProbeOrderPinsLegacyUnreadVotePatternsAbsent) {
  auto normalized_sources = load_normalized_sources();

  const std::array<Probe, 5> forbidden = {
      Probe{0, "returnget_message_content_poll_has_unread_votes(td_,m->content.get());"},
      Probe{0, "on_unread_poll_vote_removed(d,m,\"on_update_poll_has_unread_votes\");"},
      Probe{0, "send_update_chat_unread_poll_vote_count(d,\"on_update_poll_has_unread_votes\");"},
      Probe{0, "send_update_chat_unread_poll_vote_count(d,\"update_message\");"},
      Probe{1, "updateMessageUnreadPollVotes"},
  };

  for (int i = 0; i < 12000; ++i) {
    auto idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(forbidden.size()) - 1));
    const auto &probe = forbidden[idx];
    ASSERT_EQ(td::string::npos, normalized_sources[probe.source_index].find(probe.snippet.str()));
  }
}
