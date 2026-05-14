// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/utils/common.h"

#include "test/stealth/SourceContractFileReader.h"

#include <string_view>

namespace td::bot_cross_chat_reply_test {

inline bool is_contract_whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

inline td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    if (is_contract_whitespace(c)) {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

inline td::string extract_region(std::string_view source, td::Slice begin_marker, td::Slice end_marker) {
  auto begin = source.find(begin_marker.str());
  CHECK(begin != td::string::npos);
  auto end = source.find(end_marker.str(), begin + begin_marker.size());
  CHECK(end != td::string::npos);
  CHECK(end > begin);
  return td::string(source.substr(begin, end - begin));
}

inline td::string read_messages_manager_source() {
  return td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
}

inline td::string guest_message_object_region() {
  auto source = read_messages_manager_source();
  return extract_region(source, "td_api::object_ptr<td_api::message> MessagesManager::get_guest_message_object(",
                        "td_api::object_ptr<td_api::message> MessagesManager::get_message_object(DialogId dialog_id, "
                        "const Message *m,");
}

inline td::string message_object_region() {
  auto source = read_messages_manager_source();
  return extract_region(source,
                        "td_api::object_ptr<td_api::message> MessagesManager::get_message_object(DialogId dialog_id, "
                        "const Message *m,",
                        "td_api::object_ptr<td_api::messages> MessagesManager::get_messages_object(int32 total_count, "
                        "DialogId dialog_id,");
}

inline td::string normalized_guest_message_object_region() {
  return normalize_for_contract(guest_message_object_region());
}

inline td::string normalized_message_object_region() {
  return normalize_for_contract(message_object_region());
}

}  // namespace td::bot_cross_chat_reply_test