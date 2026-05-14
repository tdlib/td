// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/utils/common.h"

#include "test/stealth/SourceContractFileReader.h"

namespace td::paid_media_input_media_test {

inline bool is_contract_whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

inline td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    if (!is_contract_whitespace(c)) {
      normalized.push_back(c);
    }
  }
  return normalized;
}

inline td::string read_business_connection_manager_cpp() {
  return td::mtproto::test::read_repo_text_file("td/telegram/BusinessConnectionManager.cpp");
}

inline td::string read_message_content_cpp() {
  return td::mtproto::test::read_repo_text_file("td/telegram/MessageContent.cpp");
}

inline td::string read_message_content_h() {
  return td::mtproto::test::read_repo_text_file("td/telegram/MessageContent.h");
}

inline td::string normalized_business_connection_manager_cpp() {
  return normalize_for_contract(read_business_connection_manager_cpp());
}

inline td::string normalized_message_content_cpp() {
  return normalize_for_contract(read_message_content_cpp());
}

inline td::string normalized_message_content_h() {
  return normalize_for_contract(read_message_content_h());
}

inline size_t count_occurrences(const td::string &haystack, const td::string &needle) {
  size_t count = 0;
  size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != td::string::npos) {
    ++count;
    ++pos;
  }
  return count;
}

}  // namespace td::paid_media_input_media_test