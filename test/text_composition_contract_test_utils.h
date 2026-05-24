// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/utils/common.h"

#include <string_view>

namespace td::text_composition_contract_test {

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

inline size_t count_occurrences(std::string_view haystack, std::string_view needle) {
  size_t count = 0;
  size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string_view::npos) {
    ++count;
    ++pos;
  }
  return count;
}

}  // namespace td::text_composition_contract_test