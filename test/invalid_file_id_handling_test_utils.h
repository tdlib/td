// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/utils/common.h"

#include "test/stealth/SourceContractFileReader.h"

namespace td::invalid_file_id_handling_test {

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

inline td::string read_message_content_cpp() {
  return td::mtproto::test::read_repo_text_file("td/telegram/MessageContent.cpp");
}

inline td::string read_messages_manager_cpp() {
  return td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
}

inline td::string read_file_manager_cpp() {
  return td::mtproto::test::read_repo_text_file("td/telegram/files/FileManager.cpp");
}

inline td::string read_file_upload_id_cpp() {
  return td::mtproto::test::read_repo_text_file("td/telegram/files/FileUploadId.cpp");
}

inline td::string normalized_message_content_cpp() {
  return normalize_for_contract(read_message_content_cpp());
}

inline td::string normalized_messages_manager_cpp() {
  return normalize_for_contract(read_messages_manager_cpp());
}

inline td::string normalized_file_manager_cpp() {
  return normalize_for_contract(read_file_manager_cpp());
}

inline td::string normalized_file_upload_id_cpp() {
  return normalize_for_contract(read_file_upload_id_cpp());
}

inline td::string extract_normalized_segment(const td::string &source, td::Slice begin_marker, td::Slice end_marker) {
  auto begin_pos = source.find(begin_marker.str());
  if (begin_pos == td::string::npos) {
    return td::string();
  }
  auto end_pos = source.find(end_marker.str(), begin_pos);
  if (end_pos == td::string::npos) {
    end_pos = source.size();
  }
  return source.substr(begin_pos, end_pos - begin_pos);
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

}  // namespace td::invalid_file_id_handling_test
