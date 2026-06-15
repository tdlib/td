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
    if (auto byte = static_cast<unsigned char>(c); byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

// Upstream tdlib a74cc9af8 ("Ignore draft replies to local messages"), adapted as a fork-local
// equivalent that preserves this fork's existing is_valid_scheduled() hardening.
// Contract: when parsing a persisted draft, a same-chat reply target that is yet-unsent OR local MUST be
// cleared (fail-closed), because such message ids do not survive a restart and would otherwise become a
// dangling reply reference. The fork's superset left-hand guard (is_valid() || is_valid_scheduled())
// must remain intact.
TEST(DraftLocalReplyIgnoreContract, ParseClearsSameChatLocalAndYetUnsentReplies) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/DraftMessage.hpp");
  auto region = extract_region(source, "auto clear_same_chat_yet_unsent_reply = [this]() {",
                               "td::parse(date_, parser);");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("(message_id.is_valid()||message_id.is_valid_scheduled())&&(message_id.is_yet_unsent()"
                              "||message_id.is_local())") != td::string::npos);
  ASSERT_TRUE(normalized.find("message_input_reply_to_={};") != td::string::npos);
}

}  // namespace
