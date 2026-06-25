// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

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

td::string safe_region(const td::string &s, td::Slice begin_marker, td::Slice end_marker) {
  auto begin = s.find(begin_marker.str());
  if (begin == td::string::npos) {
    return {};
  }
  auto end = s.find(end_marker.str(), begin + begin_marker.size());
  if (end == td::string::npos || end <= begin) {
    return {};
  }
  return s.substr(begin, end - begin);
}

bool has(const td::string &s, td::Slice needle) {
  return s.find(needle.str()) != td::string::npos;
}

// Phase-2 poll-media render refactor (upstream d549c79d7 "Add td_api::PollMedia", ef5759d20). The fork
// renders poll option / quiz-explanation / attached media via a dedicated td_api::PollMedia hierarchy
// instead of generic MessageContent. The backport had to MERGE the schema change with a pre-existing
// fork fail-safe: the MessagePoll renderer first resolves the poll manager and returns messageUnsupported
// when it is inaccessible, rather than dereferencing a null poll_manager. This contract pins that the
// schema migration did not drop the fork's fail-safe.
TEST(Phase2PollMediaFailsafeContract, MessagePollRetainsPollManagerNullGuardAndUsesPollMedia) {
  auto src = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/MessageContent.cpp"));
  // the source-tag string "get_message_content_object" is unique to this call site, so it anchors the
  // get_message_content_object() MessagePoll branch (the bare `case ...::Poll:` label is not unique).
  auto region = safe_region(src, "get_poll_manager_for_content_access(td,m->poll_id,\"get_message_content_object\")",
                            "caseMessageContentType::Dice:{");
  ASSERT_TRUE(!region.empty());
  // fork fail-safe: bail out to messageUnsupported when the poll manager is inaccessible (no null deref)
  ASSERT_TRUE(has(region, "if(poll_manager==nullptr){returnmake_tl_object<td_api::messageUnsupported>();}"));
  // schema migration: attached media is produced as a PollMedia object
  ASSERT_TRUE(has(region, "td_api::object_ptr<td_api::PollMedia>media;"));
  ASSERT_TRUE(has(region, "get_poll_media_object(m->attached_media.get(),td)"));
}

}  // namespace
