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
      "td/telegram/MessageContent.cpp",
      "td/telegram/PollManager.cpp",
  };

  std::array<td::string, source_paths.size()> normalized_sources;
  for (size_t i = 0; i < source_paths.size(); ++i) {
    normalized_sources[i] = normalize_for_contract(td::mtproto::test::read_repo_text_file(source_paths[i]));
  }
  return normalized_sources;
}

}  // namespace

TEST(PollMediaSendGuardLightFuzz, RandomizedProbeOrderPinsFailClosedSendGuardPatterns) {
  auto normalized_sources = load_normalized_sources();

  const std::array<Probe, 10> required = {
      Probe{
          0,
          "if(message_content_poll_has_media(content,td)){returnStatus::Error(400,\"Pollswithmediacan'tbesentyet\");}"},
      Probe{0, "if(message_content_poll_has_media(content,td)){returnfalse;}"},
      Probe{0, "boolcan_forward_message_content(constTd*td,constMessageContent*content,boolis_copy){"},
      Probe{1, "boolPollManager::has_input_media(PollIdpoll_id)const{"},
      Probe{1, "if(!get_poll_file_ids(poll_id).empty()){returnfalse;}"},
      Probe{1, "tl_object_ptr<telegram_api::InputMedia>PollManager::get_input_media(PollIdpoll_id)const{"},
      Probe{1, "if(!get_poll_file_ids(poll_id).empty()){"},
      Probe{1, "returnnullptr;"},
      Probe{1,
            "LOG(ERROR)<<\"Fail-closedpollinputmediagenerationfor\"<<poll_id<<\":pollcontainsmediafieldsthataren'"
            "tsupportedininputMediaPoll\";"},
      Probe{1, "returntelegram_api::make_object<telegram_api::inputMediaPoll>("},
  };

  for (int i = 0; i < 12000; ++i) {
    auto idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(required.size()) - 1));
    const auto &probe = required[idx];
    ASSERT_NE(td::string::npos, normalized_sources[probe.source_index].find(probe.snippet.str()));
  }
}

TEST(PollMediaSendGuardLightFuzz, RandomizedProbeOrderPinsLegacyForwardCopyGateAbsent) {
  auto normalized_sources = load_normalized_sources();

  const std::array<Probe, 1> forbidden = {
      Probe{0,
            "if(content_type==MessageContentType::Poll){autopoll_id=static_cast<constMessagePoll*>(content)->poll_id;"
            "auto*poll_manager=get_poll_manager_for_content_access(td,poll_id,\"can_forward_message_content\");"
            "if(poll_manager==nullptr||!poll_manager->has_input_media(poll_id)){returnfalse;}}"},
  };

  for (int i = 0; i < 12000; ++i) {
    auto idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(forbidden.size()) - 1));
    const auto &probe = forbidden[idx];
    ASSERT_EQ(td::string::npos, normalized_sources[probe.source_index].find(probe.snippet.str()));
  }
}
