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

namespace td::w5_text_composition_style_name_test {

inline td::string normalize_for_contract(td::Slice source) {
  return td::text_composition_contract_test::normalize_for_contract(source);
}

inline td::string read_translation_manager_cpp() {
  return td::mtproto::test::read_repo_text_file("td/telegram/TranslationManager.cpp");
}

inline td::string normalized_translation_manager_cpp() {
  return normalize_for_contract(read_translation_manager_cpp());
}

inline td::string read_link_manager_cpp() {
  return td::mtproto::test::read_repo_text_file("td/telegram/LinkManager.cpp");
}

inline td::string normalized_link_manager_cpp() {
  return normalize_for_contract(read_link_manager_cpp());
}

inline size_t count_occurrences(std::string_view haystack, std::string_view needle) {
  return td::text_composition_contract_test::count_occurrences(haystack, needle);
}

}  // namespace td::w5_text_composition_style_name_test
