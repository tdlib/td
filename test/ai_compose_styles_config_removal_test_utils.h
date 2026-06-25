// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/utils/common.h"

#include "test/stealth/SourceContractFileReader.h"

#include <string_view>

namespace td::ai_compose_styles_config_removal_test {

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

inline td::string read_config_manager_cpp() {
  return td::mtproto::test::read_repo_text_file("td/telegram/ConfigManager.cpp");
}

inline td::string read_config_manager_h() {
  return td::mtproto::test::read_repo_text_file("td/telegram/ConfigManager.h");
}

inline td::string normalized_config_manager_cpp() {
  return normalize_for_contract(read_config_manager_cpp());
}

inline td::string normalized_config_manager_h() {
  return normalize_for_contract(read_config_manager_h());
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

}  // namespace td::ai_compose_styles_config_removal_test
