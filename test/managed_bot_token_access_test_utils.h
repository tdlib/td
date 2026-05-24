// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/utils/common.h"

#include "test/stealth/SourceContractFileReader.h"

#include <string_view>

namespace td::managed_bot_token_access_test {

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

inline td::string try_extract_region(std::string_view source, td::Slice begin_marker, td::Slice end_marker) {
  auto begin = source.find(begin_marker.str());
  if (begin == td::string::npos) {
    return {};
  }
  auto end = source.find(end_marker.str(), begin + begin_marker.size());
  if (end == td::string::npos || end <= begin) {
    return {};
  }
  return td::string(source.substr(begin, end - begin));
}

inline td::string read_td_api_source() {
  return td::mtproto::test::read_repo_text_file("td/generate/scheme/td_api.tl");
}

inline td::string read_requests_cpp_source() {
  return td::mtproto::test::read_repo_text_file("td/telegram/Requests.cpp");
}

inline td::string read_requests_h_source() {
  return td::mtproto::test::read_repo_text_file("td/telegram/Requests.h");
}

inline td::string read_bot_info_manager_cpp_source() {
  return td::mtproto::test::read_repo_text_file("td/telegram/BotInfoManager.cpp");
}

inline td::string read_managed_bot_token_access_h_source() {
  return td::mtproto::test::read_repo_text_file("td/telegram/ManagedBotTokenAccess.h");
}

inline td::string read_cli_source() {
  return td::mtproto::test::read_repo_text_file("td/telegram/cli.cpp");
}

inline td::string normalized_td_api_source() {
  return normalize_for_contract(read_td_api_source());
}

inline td::string normalized_requests_cpp_source() {
  return normalize_for_contract(read_requests_cpp_source());
}

inline td::string normalized_requests_h_source() {
  return normalize_for_contract(read_requests_h_source());
}

inline td::string normalized_bot_info_manager_cpp_source() {
  return normalize_for_contract(read_bot_info_manager_cpp_source());
}

inline td::string normalized_managed_bot_token_access_h_source() {
  return normalize_for_contract(read_managed_bot_token_access_h_source());
}

inline td::string normalized_cli_source() {
  return normalize_for_contract(read_cli_source());
}

inline td::string get_bot_token_function_region() {
  auto source = read_bot_info_manager_cpp_source();
  return try_extract_region(source,
                            "void BotInfoManager::get_bot_token(UserId bot_user_id, bool revoke, Promise<string> "
                            "&&promise) {",
                            "void BotInfoManager::get_owned_bots(Promise<td_api::object_ptr<td_api::users>> "
                            "&&promise) {");
}

inline td::string requests_get_bot_token_handler_region() {
  auto source = read_requests_cpp_source();
  return try_extract_region(source, "void Requests::on_request(uint64 id, const td_api::getBotToken &request) {",
                            "void Requests::on_request(uint64 id, const td_api::getManagedBotToken &request) {");
}

inline td::string requests_get_managed_bot_token_handler_region() {
  auto source = read_requests_cpp_source();
  return try_extract_region(source, "void Requests::on_request(uint64 id, const td_api::getManagedBotToken &request) {",
                            "void Requests::on_request(uint64 id, td_api::setBotName &request) {");
}

inline td::string requests_dispatch_get_managed_bot_token_region() {
  auto source = read_requests_cpp_source();
  return try_extract_region(source,
                            "void Requests::dispatch_get_managed_bot_token(int64 bot_user_id, bool revoke, "
                            "Promise<string> &&promise) {",
                            "void Requests::on_request(uint64 id, td_api::setBotName &request) {");
}

inline size_t count_substring(td::Slice haystack, td::Slice needle) {
  if (needle.empty()) {
    return 0;
  }
  size_t count = 0;
  size_t pos = 0;
  auto haystack_str = haystack.str();
  auto needle_str = needle.str();
  while (true) {
    pos = haystack_str.find(needle_str, pos);
    if (pos == td::string::npos) {
      return count;
    }
    count++;
    pos += needle_str.size();
  }
}

}  // namespace td::managed_bot_token_access_test
