// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/utils/common.h"

#include "test/stealth/SourceContractFileReader.h"
#include "test/text_composition_contract_test_utils.h"

#include <string_view>

namespace td::text_composition_style_example_count_option_test {

inline td::string normalize_for_contract(td::Slice source) {
  return td::text_composition_contract_test::normalize_for_contract(source);
}

inline td::string read_config_manager_cpp() {
  return td::mtproto::test::read_repo_text_file("td/telegram/ConfigManager.cpp");
}

inline td::string read_option_manager_cpp() {
  return td::mtproto::test::read_repo_text_file("td/telegram/OptionManager.cpp");
}

inline td::string read_config_manager_h() {
  return td::mtproto::test::read_repo_text_file("td/telegram/ConfigManager.h");
}

inline td::string normalized_config_manager_cpp() {
  return normalize_for_contract(read_config_manager_cpp());
}

inline td::string normalized_option_manager_cpp() {
  return normalize_for_contract(read_option_manager_cpp());
}

inline td::string normalized_config_manager_h() {
  return normalize_for_contract(read_config_manager_h());
}

inline size_t count_occurrences(std::string_view haystack, std::string_view needle) {
  return td::text_composition_contract_test::count_occurrences(haystack, needle);
}

}  // namespace td::text_composition_style_example_count_option_test
