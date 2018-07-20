//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/NetQuery.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/Container.h"

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace td {

class LanguagePackManager : public NetQueryCallback {
 public:
  explicit LanguagePackManager(ActorShared<> parent) : parent_(std::move(parent)) {
  }

  void on_language_pack_changed();

  void on_language_code_changed();

  void on_language_pack_version_changed(int32 new_version);

  void get_languages(Promise<td_api::object_ptr<td_api::languagePack>> promise);

  void get_language_pack_strings(string language_code, vector<string> keys,
                                 Promise<td_api::object_ptr<td_api::languagePackStrings>> promise);

  void on_update_language_pack(tl_object_ptr<telegram_api::langPackDifference> difference);

 private:
  struct PluralizedString {
    string zero_value_;
    string one_value_;
    string two_value_;
    string few_value_;
    string many_value_;
    string other_value_;
  };

  struct Language {
    std::mutex mutex_;
    std::atomic<int32> version_{-1};
    bool has_get_difference_query_ = false;
    std::unordered_map<string, string> ordinary_strings_;
    std::unordered_map<string, PluralizedString> pluralized_strings_;
    std::unordered_set<string> deleted_strings_;
  };

  struct LanguagePack {
    std::mutex mutex_;
    std::unordered_map<string, std::unique_ptr<Language>> languages_;
  };

  struct LanguageDatabase {
    std::mutex mutex_;
    string path_;
    std::unordered_map<string, std::unique_ptr<LanguagePack>> language_packs_;
  };

  ActorShared<> parent_;

  string language_pack_;
  string language_code_;
  LanguageDatabase *database_;
  uint32 generation_ = 0;

  static int32 manager_count_;

  static std::mutex language_database_mutex_;
  static std::unordered_map<string, std::unique_ptr<LanguageDatabase>> language_databases_;

  static Language *get_language(LanguageDatabase *database, const string &language_pack, const string &language_code);
  static Language *get_language(LanguagePack *language_pack, const string &language_code);

  static Language *add_language(LanguageDatabase *database, const string &language_pack, const string &language_code);

  static bool language_has_string_unsafe(Language *language, const string &key);
  static bool language_has_strings(Language *language, const vector<string> &keys);

  static td_api::object_ptr<td_api::LanguagePackString> get_language_pack_string_object(
      const std::pair<string, string> &str);
  static td_api::object_ptr<td_api::LanguagePackString> get_language_pack_string_object(
      const std::pair<string, PluralizedString> &str);
  static td_api::object_ptr<td_api::LanguagePackString> get_language_pack_string_object(const string &str);

  static td_api::object_ptr<td_api::languagePackStrings> get_language_pack_strings_object(Language *language,
                                                                                          const vector<string> &keys);

  void inc_generation();

  void on_get_language_pack_strings(string language_pack, string language_code, int32 version, bool is_diff,
                                    vector<string> keys, vector<tl_object_ptr<telegram_api::LangPackString>> results,
                                    Promise<td_api::object_ptr<td_api::languagePackStrings>> promise);

  void on_failed_get_difference(string language_pack, string language_code);

  void on_result(NetQueryPtr query) override;

  void start_up() override;
  void hangup() override;
  void tear_down() override;

  Container<Promise<NetQueryPtr>> container_;
  void send_with_promise(NetQueryPtr query, Promise<NetQueryPtr> promise);
};

}  // namespace td
