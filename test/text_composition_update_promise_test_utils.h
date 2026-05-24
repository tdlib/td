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

namespace td::w5_text_composition_update_promise_test {

inline td::string normalize_for_contract(td::Slice source) {
  return td::text_composition_contract_test::normalize_for_contract(source);
}

inline td::string read_translation_manager_h() {
  return td::mtproto::test::read_repo_text_file("td/telegram/TranslationManager.h");
}

inline td::string read_translation_manager_cpp() {
  return td::mtproto::test::read_repo_text_file("td/telegram/TranslationManager.cpp");
}

inline td::string normalized_translation_manager_h() {
  return normalize_for_contract(read_translation_manager_h());
}

inline td::string normalized_translation_manager_cpp() {
  return normalize_for_contract(read_translation_manager_cpp());
}

inline td::string promise_update_overload_signature() {
  return "voidTranslationManager::on_update_ai_compose_styles(vector<string>&&ai_compose_styles,Promise<Unit>&&promise)"
         "{";
}

inline td::string promise_update_delegate_call() {
  return "on_update_ai_compose_styles(std::move(ai_compose_styles));";
}

inline td::string promise_update_completion_call() {
  return "std::move(promise).set_value(Unit());";
}

inline td::string promise_update_overload_body() {
  auto normalized = normalized_translation_manager_cpp();
  auto signature = promise_update_overload_signature();
  auto start_pos = normalized.find(signature);
  if (start_pos == td::string::npos) {
    return {};
  }
  start_pos += signature.size();
  auto end_pos = normalized.find('}', start_pos);
  if (end_pos == td::string::npos) {
    return {};
  }
  return normalized.substr(start_pos, end_pos - start_pos);
}

inline size_t count_occurrences(std::string_view haystack, std::string_view needle) {
  return td::text_composition_contract_test::count_occurrences(haystack, needle);
}

}  // namespace td::w5_text_composition_update_promise_test