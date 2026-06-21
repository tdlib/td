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

TEST(CallNotificationSendClosureLaterAdversarial, AddPathMustStayDeferredWhileRemovePathStaysImmediate) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/CallActor.cpp");
  auto region = extract_region(source, "void CallActor::flush_call_state() {", "void CallActor::start_up() {");
  auto normalized = normalize_for_contract(region);

  auto set_flag_pos = normalized.find("has_notification_=true;");
  auto helper_pos = normalized.find("autonotification_action=get_pending_call_notification_action(");
  auto add_later_pos = normalized.find(
      "send_closure_later(G()->notification_manager(),&NotificationManager::add_call_notification,");
  auto remove_now_pos =
      normalized.find("send_closure(G()->notification_manager(),&NotificationManager::remove_call_notification,");

  ASSERT_EQ(td::string::npos,
            normalized.find("send_closure(G()->notification_manager(),&NotificationManager::add_call_notification,"));
  ASSERT_EQ(td::string::npos,
            normalized.find("send_closure_later(G()->notification_manager(),&NotificationManager::remove_call_"
                            "notification,"));
  ASSERT_NE(td::string::npos, helper_pos);
  ASSERT_NE(td::string::npos, set_flag_pos);
  ASSERT_NE(td::string::npos, add_later_pos);
  ASSERT_NE(td::string::npos, remove_now_pos);
  ASSERT_TRUE(helper_pos < set_flag_pos);
  ASSERT_TRUE(set_flag_pos < add_later_pos);
}

}  // namespace
