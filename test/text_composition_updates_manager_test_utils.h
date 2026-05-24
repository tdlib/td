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

namespace td::w5_text_composition_updates_manager_test {

inline td::string normalize_for_contract(td::Slice source) {
  return td::text_composition_contract_test::normalize_for_contract(source);
}

inline td::string read_updates_manager_h() {
  return td::mtproto::test::read_repo_text_file("td/telegram/UpdatesManager.h");
}

inline td::string read_updates_manager_cpp() {
  return td::mtproto::test::read_repo_text_file("td/telegram/UpdatesManager.cpp");
}

inline td::string read_telegram_api_tl() {
  return td::mtproto::test::read_repo_text_file("td/generate/scheme/telegram_api.tl");
}

inline td::string normalized_updates_manager_h() {
  return normalize_for_contract(read_updates_manager_h());
}

inline td::string normalized_updates_manager_cpp() {
  return normalize_for_contract(read_updates_manager_cpp());
}

inline td::string normalized_telegram_api_tl() {
  return normalize_for_contract(read_telegram_api_tl());
}

inline td::string ai_compose_styles_bridge_declaration() {
  return "voidon_update_ai_compose_styles(vector<string>&&ai_compose_styles,Promise<Unit>&&promise);";
}

inline td::string ai_compose_styles_bridge_signature() {
  return "voidUpdatesManager::on_update_ai_compose_styles(vector<string>&&ai_compose_styles,Promise<Unit>&&promise){";
}

inline td::string ai_compose_styles_bridge_bot_guard() {
  return "if(td_->auth_manager_->is_bot()){returnpromise.set_value(Unit());}";
}

inline td::string ai_compose_styles_bridge_dispatch_call() {
  return "send_closure(td_->translation_manager_actor_,static_cast<void(TranslationManager::*)(vector<string>&&,"
         "Promise<Unit>&&)>(&TranslationManager::on_update_ai_compose_styles),std::move(ai_compose_styles),std::move("
         "promise));";
}

inline td::string ai_compose_tones_update_constructor() {
  return "updateAiComposeTones#8c0f91fb=Update;";
}

inline td::string ai_compose_tones_update_declaration() {
  return "voidon_update(tl_object_ptr<telegram_api::updateAiComposeTones>update,Promise<Unit>&&promise);";
}

inline td::string ai_compose_tones_update_signature() {
  return "voidUpdatesManager::on_update(tl_object_ptr<telegram_api::updateAiComposeTones>update,Promise<Unit>&&promise)"
         "{";
}

inline td::string ai_compose_tones_update_reload_call() {
  return "td_->translation_manager_->reload_ai_compose_tones(Auto());";
}

inline td::string ai_compose_tones_update_bot_guard() {
  // The bot-session guard is delegated to dispatch_ai_compose_tones_update whose first argument
  // is the is_bot() predicate.  The inline early-return pattern was replaced by the dispatch
  // helper abstraction; we pin the call-site needle instead.
  return "dispatch_ai_compose_tones_update(td_->auth_manager_->is_bot(),";
}

inline td::string ai_compose_tones_update_promise_completion() {
  return "std::move(promise).set_value(Unit());";
}

inline td::string extract_function_body(std::string_view normalized, std::string_view signature) {
  auto start_pos = normalized.find(signature);
  if (start_pos == std::string_view::npos) {
    return {};
  }
  start_pos += signature.size();

  int depth = 1;
  for (size_t i = start_pos; i < normalized.size(); i++) {
    auto c = normalized[i];
    if (c == '{') {
      depth++;
      continue;
    }
    if (c != '}') {
      continue;
    }
    depth--;
    if (depth == 0) {
      return td::string(normalized.substr(start_pos, i - start_pos));
    }
  }

  return {};
}

inline td::string ai_compose_styles_bridge_body() {
  auto normalized = normalized_updates_manager_cpp();
  return extract_function_body(normalized, ai_compose_styles_bridge_signature());
}

inline td::string ai_compose_tones_update_body() {
  auto normalized = normalized_updates_manager_cpp();
  return extract_function_body(normalized, ai_compose_tones_update_signature());
}

inline size_t count_occurrences(std::string_view haystack, std::string_view needle) {
  return td::text_composition_contract_test::count_occurrences(haystack, needle);
}

}  // namespace td::w5_text_composition_updates_manager_test
