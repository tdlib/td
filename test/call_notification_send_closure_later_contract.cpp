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

// Upstream tdlib c3759d5c5 ("Use send_closure_later when adding call notification just in case").
// Contract: the pending-incoming-call notification must be posted via send_closure_later, not
// send_closure, so it is never delivered re-entrantly inside the current actor turn (ordering /
// reentrancy hardening on the call lifecycle).
TEST(CallNotificationSendClosureLaterContract, PendingCallNotificationIsPostedDeferred) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/CallActor.cpp");
  auto region = extract_region(source, "if (call_state_.type == CallState::Type::Pending) {", "} else {");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("send_closure_later(G()->notification_manager(),&NotificationManager::add_call_notification") !=
              td::string::npos);
}

}  // namespace
