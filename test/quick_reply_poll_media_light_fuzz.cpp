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
    switch (static_cast<unsigned char>(c)) {
      case ' ':
      case '\t':
      case '\r':
      case '\n':
        continue;
      default:
        break;
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
      "td/telegram/QuickReplyManager.cpp",
      "td/telegram/MessageContent.cpp",
  };

  std::array<td::string, source_paths.size()> normalized_sources;
  for (size_t i = 0; i < source_paths.size(); ++i) {
    normalized_sources[i] = normalize_for_contract(td::mtproto::test::read_repo_text_file(source_paths[i]));
  }
  return normalized_sources;
}

}  // namespace

TEST(QuickReplyPollMediaLightFuzz, RandomizedProbeOrderPinsAnyMediaGuardPatterns) {
  auto normalized_sources = load_normalized_sources();

  const std::array<Probe, 11> required = {
      Probe{0, "if(message_content_poll_has_media(content,td_)){"},
      Probe{0, "if(message_content_poll_has_media(message->content.get(),td_)){"},
      Probe{0, "Can'tsendpollswithmediafromquickreplies"},
      Probe{1, "boolmessage_content_poll_has_media(constMessageContent*content,constTd*td){"},
      Probe{1, "if(content==nullptr){returnfalse;}"},
      Probe{1, "if(content->get_type()!=MessageContentType::Poll){returnfalse;}"},
      Probe{1, "if(td==nullptr){"},
      Probe{1, "if(poll->attached_media!=nullptr){returntrue;}"},
      Probe{1,
            "auto*poll_manager=get_poll_manager_for_content_access(td,poll->poll_id,\"message_content_poll_has_media\")"
            ";"},
      Probe{1, "if(poll_manager==nullptr){returntrue;}"},
      Probe{1, "return!poll_manager->get_poll_file_ids(poll->poll_id).empty();"},
  };

  for (int i = 0; i < 12000; ++i) {
    auto idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(required.size()) - 1));
    const auto &probe = required[idx];
    ASSERT_NE(td::string::npos, normalized_sources[probe.source_index].find(probe.snippet.str()));
  }
}

TEST(QuickReplyPollMediaLightFuzz, RandomizedProbeOrderPinsLegacyAttachedOnlyPatternsAbsent) {
  auto normalized_sources = load_normalized_sources();

  const std::array<Probe, 4> forbidden = {
      Probe{0, "message_content_poll_has_attached_media(content)"},
      Probe{0, "message_content_poll_has_attached_media(message->content.get())"},
      Probe{1, "boolmessage_content_poll_has_attached_media(constMessageContent*content){"},
      Probe{1, "returnstatic_cast<constMessagePoll*>(content)->attached_media!=nullptr;"},
  };

  for (int i = 0; i < 12000; ++i) {
    auto idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(forbidden.size()) - 1));
    const auto &probe = forbidden[idx];
    ASSERT_EQ(td::string::npos, normalized_sources[probe.source_index].find(probe.snippet.str()));
  }
}
