// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/utils/common.h"

#include "test/stealth/SourceContractFileReader.h"

#include <string_view>

namespace td::message_link_info_consumer_test {

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

inline td::string read_repo_text(td::Slice path) {
  return td::mtproto::test::read_repo_text_file(path);
}

inline td::string read_normalized(td::Slice path) {
  return normalize_for_contract(read_repo_text(path));
}

inline td::string extract_region(td::Slice source, td::Slice begin_marker, td::Slice end_marker) {
  auto source_text = source.str();
  auto begin = source_text.find(begin_marker.str());
  CHECK(begin != td::string::npos);
  auto end = source_text.find(end_marker.str(), begin + begin_marker.size());
  CHECK(end != td::string::npos);
  CHECK(begin < end);
  return source_text.substr(begin, end - begin);
}

inline td::string extract_normalized_region(td::Slice path, td::Slice begin_marker, td::Slice end_marker) {
  return normalize_for_contract(extract_region(read_repo_text(path), begin_marker, end_marker));
}

inline size_t count_occurrences(std::string_view haystack, std::string_view needle) {
  size_t count = 0;
  size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string_view::npos) {
    ++count;
    ++pos;
  }
  return count;
}

inline td::string read_message_content_header() {
  return read_repo_text("td/telegram/MessageContent.h");
}

inline td::string read_normalized_messages_manager() {
  return read_normalized("td/telegram/MessagesManager.cpp");
}

inline td::string read_normalized_web_pages_manager() {
  return read_normalized("td/telegram/WebPagesManager.cpp");
}

inline td::string read_normalized_generator_region() {
  return extract_normalized_region(
      "td/telegram/MessagesManager.cpp",
      "Result<std::pair<string, bool>> MessagesManager::get_message_link(MessageFullId message_full_id, "
      "int32 media_timestamp,",
      "Status MessagesManager::can_get_message_embedding_code(DialogId dialog_id, const Message *m) const {");
}

inline td::string read_normalized_consumer_region() {
  return extract_normalized_region(
      "td/telegram/MessagesManager.cpp",
      "td_api::object_ptr<td_api::messageLinkInfo> MessagesManager::get_message_link_info_object(",
      "Status MessagesManager::can_add_dialog_to_filter(DialogId dialog_id) {");
}

inline td::string read_normalized_video_timestamp_region() {
  return extract_normalized_region(
      "td/telegram/WebPagesManager.cpp", "int32 WebPagesManager::get_video_start_timestamp(",
      "td_api::object_ptr<td_api::LinkPreviewType> WebPagesManager::get_link_preview_type_album_object(");
}

}  // namespace td::message_link_info_consumer_test