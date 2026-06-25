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

// Non-aborting region slice: returns "" if either marker is absent.
td::string safe_region(const td::string &normalized_source, td::Slice begin_marker, td::Slice end_marker) {
  auto begin = normalized_source.find(begin_marker.str());
  if (begin == td::string::npos) {
    return {};
  }
  auto end = normalized_source.find(end_marker.str(), begin + begin_marker.size());
  if (end == td::string::npos || end <= begin) {
    return {};
  }
  return normalized_source.substr(begin, end - begin);
}

bool has(const td::string &s, td::Slice needle) {
  return s.find(needle.str()) != td::string::npos;
}

// Phase-2 in-app web browser epic (upstream cluster fad8f9afe..6d0824e37). The fork carries the
// WebBrowserManager actor that pushes/reloads the user's web-browser settings. These contracts pin the
// fail-closed guards that keep the feature off the bot/unauthorized lane — a bot or an unauthorized
// client must never receive or trigger web-browser-settings traffic.

// 6d0824e37 — "Check for bots in WebBrowserManager::on_authorization_success". On (re)authorization the
// manager must NOT push web-browser settings to a bot session. The guard is an early bot-return at the
// top of on_authorization_success.
TEST(Phase2WebBrowserFailClosedContract, OnAuthorizationSuccessSkipsBots) {
  auto cpp = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/WebBrowserManager.cpp"));
  auto region = safe_region(cpp, "WebBrowserManager::on_authorization_success(){",
                            "WebBrowserManager::get_web_browser_settings_database_key(");
  ASSERT_TRUE(!region.empty());
  ASSERT_TRUE(has(region, "if(td_->auth_manager_->is_bot()){return;}"));
}

// Persistence load must be fail-closed: an unauthorized OR bot session never loads/uses stored settings.
TEST(Phase2WebBrowserFailClosedContract, LoadSettingsFailClosed) {
  auto cpp = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/WebBrowserManager.cpp"));
  auto region = safe_region(cpp, "WebBrowserManager::load_web_browser_settings(){", "get_web_browser_settings_database_key())");
  ASSERT_TRUE(!region.empty());
  ASSERT_TRUE(has(region, "if(!td_->auth_manager_->is_authorized()||td_->auth_manager_->is_bot()){return;}"));
}

// getCurrentState emission must be fail-closed for the same reason: no web-browser-settings update is
// surfaced to an unauthorized or bot session.
TEST(Phase2WebBrowserFailClosedContract, GetCurrentStateFailClosed) {
  auto cpp = normalize_for_contract(td::mtproto::test::read_repo_text_file("td/telegram/WebBrowserManager.cpp"));
  auto region = safe_region(cpp, "WebBrowserManager::get_current_state(", "updates.push_back(");
  ASSERT_TRUE(!region.empty());
  ASSERT_TRUE(has(region, "if(!td_->auth_manager_->is_authorized()||td_->auth_manager_->is_bot()){return;}"));
}

}  // namespace
