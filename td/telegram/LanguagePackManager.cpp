//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/LanguagePackManager.h"

#include "td/telegram/ConfigShared.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/Td.h"

#include "td/telegram/td_api.h"
#include "td/telegram/td_api.hpp"
#include "td/telegram/telegram_api.h"

#include "td/db/SqliteDb.h"
#include "td/db/SqliteKeyValue.h"

#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Status.h"

#include <atomic>
#include <limits>
#include <map>
#include <unordered_set>
#include <utility>

namespace td {

struct LanguagePackManager::PluralizedString {
  string zero_value_;
  string one_value_;
  string two_value_;
  string few_value_;
  string many_value_;
  string other_value_;
};

struct LanguagePackManager::Language {
  std::mutex mutex_;
  std::atomic<int32> version_{-1};
  std::atomic<int32> key_count_{0};
  bool is_full_ = false;
  bool was_loaded_full_ = false;
  bool has_get_difference_query_ = false;
  std::unordered_map<string, string> ordinary_strings_;
  std::unordered_map<string, PluralizedString> pluralized_strings_;
  std::unordered_set<string> deleted_strings_;
  SqliteKeyValue kv_;  // usages should be guarded by database_->mutex_
};

struct LanguagePackManager::LanguageInfo {
  string name;
  string native_name;

  friend bool operator==(const LanguageInfo &lhs, const LanguageInfo &rhs) {
    return lhs.name == rhs.name && lhs.native_name == rhs.native_name;
  }
};

struct LanguagePackManager::LanguagePack {
  std::mutex mutex_;
  SqliteKeyValue pack_kv_;                                              // usages should be guarded by database_->mutex_
  std::map<string, LanguageInfo> custom_language_pack_infos_;           // sorted by language_code
  vector<std::pair<string, LanguageInfo>> server_language_pack_infos_;  // sorted by server
  std::unordered_map<string, std::unique_ptr<Language>> languages_;
};

struct LanguagePackManager::LanguageDatabase {
  std::mutex mutex_;
  string path_;
  SqliteDb database_;
  std::unordered_map<string, std::unique_ptr<LanguagePack>> language_packs_;
};

LanguagePackManager::~LanguagePackManager() = default;

bool LanguagePackManager::check_language_pack_name(Slice name) {
  for (auto c : name) {
    if (c != '_' && !is_alpha(c)) {
      return false;
    }
  }
  return name.size() <= 64;
}

bool LanguagePackManager::check_language_code_name(Slice name) {
  for (auto c : name) {
    if (c != '-' && !is_alpha(c) && !is_digit(c)) {
      return false;
    }
  }
  return name.size() <= 64 && (is_custom_language_code(name) || name.empty() || name.size() >= 2);
}

bool LanguagePackManager::is_custom_language_code(Slice language_code) {
  return !language_code.empty() && language_code[0] == 'X';
}

static Result<SqliteDb> open_database(const string &path) {
  TRY_RESULT(database, SqliteDb::open_with_key(path, DbKey::empty()));
  TRY_STATUS(database.exec("PRAGMA synchronous=NORMAL"));
  TRY_STATUS(database.exec("PRAGMA temp_store=MEMORY"));
  TRY_STATUS(database.exec("PRAGMA encoding=\"UTF-8\""));
  TRY_STATUS(database.exec("PRAGMA journal_mode=WAL"));
  return std::move(database);
}

static int32 load_database_language_version(SqliteKeyValue *kv) {
  CHECK(kv != nullptr);
  if (kv->empty()) {
    return -1;
  }
  string str_version = kv->get("!version");
  if (str_version.empty()) {
    return -1;
  }

  return to_integer<int32>(str_version);
}

static int32 load_database_language_key_count(SqliteKeyValue *kv) {
  CHECK(kv != nullptr);
  if (kv->empty()) {
    return 0;
  }
  string str_key_count = kv->get("!key_count");
  if (str_key_count.empty()) {
    // calculate key count once for the database and cache it
    int key_count = 0;
    for (auto &str : kv->get_all()) {
      key_count += str.first[0] != '!' && (str.second[0] == '1' || str.second[0] == '2');
    }
    LOG(INFO) << "Set language pack key count in database to " << key_count;
    kv->set("!key_count", to_string(key_count));
    return key_count;
  }

  return to_integer<int32>(str_key_count);
}

LanguagePackManager::LanguageDatabase *LanguagePackManager::add_language_database(const string &path) {
  auto it = language_databases_.find(path);
  if (it != language_databases_.end()) {
    return it->second.get();
  }

  SqliteDb database;
  if (!path.empty()) {
    auto r_database = open_database(path);
    if (r_database.is_error()) {
      LOG(ERROR) << "Can't open language pack database " << path << ": " << r_database.error();
      return add_language_database(string());
    }

    database = r_database.move_as_ok();
  }

  it = language_databases_.emplace(path, make_unique<LanguageDatabase>()).first;
  it->second->path_ = std::move(path);
  it->second->database_ = std::move(database);
  return it->second.get();
}

void LanguagePackManager::start_up() {
  std::lock_guard<std::mutex> lock(language_database_mutex_);
  manager_count_++;
  language_pack_ = G()->shared_config().get_option_string("localization_target");
  language_code_ = G()->shared_config().get_option_string("language_pack_id");
  CHECK(check_language_pack_name(language_pack_));
  CHECK(check_language_code_name(language_code_));

  database_ = add_language_database(G()->shared_config().get_option_string("language_pack_database_path"));
  if (!language_pack_.empty() && !language_code_.empty()) {
    auto language = add_language(database_, language_pack_, language_code_);
    if (!is_custom_language_code(language_code_) && language->version_ == -1) {
      get_language_pack_strings(language_code_, vector<string>(), Auto());
    }

    LOG(INFO) << "Use localization target \"" << language_pack_ << "\" with language pack \"" << language_code_
              << "\" of version " << language->version_.load() << " with database \"" << database_->path_ << '"';
  }
}

void LanguagePackManager::tear_down() {
  std::lock_guard<std::mutex> lock(language_database_mutex_);
  manager_count_--;
  if (manager_count_ == 0) {
    // can't clear language packs, because they may be accessed later using synchronous requests
    // LOG(INFO) << "Clear language packs";
    // language_databases_.clear();
  }
}

void LanguagePackManager::on_language_pack_changed() {
  auto new_language_pack = G()->shared_config().get_option_string("localization_target");
  if (new_language_pack == language_pack_) {
    return;
  }

  language_pack_ = std::move(new_language_pack);
  CHECK(check_language_pack_name(language_pack_));
  inc_generation();
}

void LanguagePackManager::on_language_code_changed() {
  auto new_language_code = G()->shared_config().get_option_string("language_pack_id");
  if (new_language_code == language_code_) {
    return;
  }

  language_code_ = std::move(new_language_code);
  CHECK(check_language_code_name(language_code_));
  inc_generation();
}

void LanguagePackManager::on_language_pack_version_changed(int32 new_version) {
  if (is_custom_language_code(language_code_) || language_pack_.empty() || language_code_.empty()) {
    return;
  }

  Language *language = get_language(database_, language_pack_, language_code_);
  int32 version = language == nullptr ? static_cast<int32>(-1) : language->version_.load();
  if (version == -1) {
    get_language_pack_strings(language_code_, vector<string>(), Auto());
    return;
  }

  auto new_language_pack_version =
      new_version >= 0 ? new_version : G()->shared_config().get_option_integer("language_pack_version", -1);
  if (new_language_pack_version <= version) {
    return;
  }

  std::lock_guard<std::mutex> lock(language->mutex_);
  if (language->has_get_difference_query_) {
    return;
  }

  language->has_get_difference_query_ = true;
  auto request_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), language_pack = language_pack_, language_code = language_code_,
                              from_version = version](Result<NetQueryPtr> r_query) mutable {
        auto r_result = fetch_result<telegram_api::langpack_getDifference>(std::move(r_query));
        if (r_result.is_error()) {
          send_closure(actor_id, &LanguagePackManager::on_failed_get_difference, std::move(language_pack),
                       std::move(language_code));
          return;
        }

        auto result = r_result.move_as_ok();
        LOG(INFO) << "Receive language pack difference for language pack " << result->lang_code_ << " from version "
                  << result->from_version_ << " with version " << result->version_ << " of size "
                  << result->strings_.size();
        LOG_IF(ERROR, result->lang_code_ != language_code)
            << "Receive strings for " << result->lang_code_ << " instead of " << language_code;
        LOG_IF(ERROR, result->from_version_ != from_version)
            << "Receive strings from " << result->from_version_ << " instead of " << from_version;
        send_closure(actor_id, &LanguagePackManager::on_get_language_pack_strings, std::move(language_pack),
                     std::move(language_code), result->version_, true, vector<string>(), std::move(result->strings_),
                     Promise<td_api::object_ptr<td_api::languagePackStrings>>());
      });
  send_with_promise(G()->net_query_creator().create(create_storer(telegram_api::langpack_getDifference(version))),
                    std::move(request_promise));
}

void LanguagePackManager::on_update_language_pack(tl_object_ptr<telegram_api::langPackDifference> difference) {
  LOG(INFO) << "Receive update language pack difference for language pack " << difference->lang_code_
            << " from version " << difference->from_version_ << " with version " << difference->version_ << " of size "
            << difference->strings_.size();
  if (language_pack_.empty()) {
    LOG(WARNING) << "Ignore difference for language pack " << difference->lang_code_
                 << ", because used language pack was unset";
    return;
  }
  if (difference->lang_code_ != language_code_) {
    LOG(WARNING) << "Ignore difference for language pack " << difference->lang_code_;
    return;
  }
  if (is_custom_language_code(difference->lang_code_) || difference->lang_code_.empty()) {
    LOG(ERROR) << "Ignore difference for language pack " << difference->lang_code_;
    return;
  }

  Language *language = get_language(database_, language_pack_, language_code_);
  int32 version = language == nullptr ? static_cast<int32>(-1) : language->version_.load();
  if (difference->version_ <= version) {
    LOG(INFO) << "Skip applying already applied language pack updates";
    return;
  }
  if (version == -1 || version < difference->from_version_) {
    LOG(INFO) << "Can't apply language pack difference";
    return on_language_pack_version_changed(difference->version_);
  }

  on_get_language_pack_strings(language_pack_, std::move(difference->lang_code_), difference->version_, true,
                               vector<string>(), std::move(difference->strings_),
                               Promise<td_api::object_ptr<td_api::languagePackStrings>>());
}

void LanguagePackManager::inc_generation() {
  G()->shared_config().set_option_empty("language_pack_version");
  on_language_pack_version_changed(std::numeric_limits<int32>::max());
}

LanguagePackManager::Language *LanguagePackManager::get_language(LanguageDatabase *database,
                                                                 const string &language_pack,
                                                                 const string &language_code) {
  std::unique_lock<std::mutex> lock(database->mutex_);
  auto it = database->language_packs_.find(language_pack);
  if (it == database->language_packs_.end()) {
    return nullptr;
  }
  LanguagePack *pack = it->second.get();
  lock.unlock();
  return get_language(pack, language_code);
}

LanguagePackManager::Language *LanguagePackManager::get_language(LanguagePack *language_pack,
                                                                 const string &language_code) {
  CHECK(language_pack != nullptr);
  std::lock_guard<std::mutex> lock(language_pack->mutex_);
  auto it = language_pack->languages_.find(language_code);
  if (it == language_pack->languages_.end()) {
    return nullptr;
  }
  return it->second.get();
}

static string get_database_table_name(const string &language_pack, const string &language_code) {
  return PSTRING() << "\"kv_" << language_pack << '_' << language_code << '"';
}

LanguagePackManager::Language *LanguagePackManager::add_language(LanguageDatabase *database,
                                                                 const string &language_pack,
                                                                 const string &language_code) {
  std::lock_guard<std::mutex> packs_lock(database->mutex_);
  auto pack_it = database->language_packs_.find(language_pack);
  if (pack_it == database->language_packs_.end()) {
    auto pack = make_unique<LanguagePack>();
    if (!database->database_.empty()) {
      pack->pack_kv_.init_with_connection(database->database_.clone(), get_database_table_name(language_pack, "0"))
          .ensure();
      for (auto &lang : pack->pack_kv_.get_all()) {
        if (lang.first == "!server") {
          auto all_infos = full_split(lang.second, '\x00');
          if (all_infos.size() % 3 == 0) {
            for (size_t i = 0; i < all_infos.size(); i += 3) {
              LanguageInfo info;
              info.name = std::move(all_infos[i + 1]);
              info.native_name = std::move(all_infos[i + 2]);
              pack->server_language_pack_infos_.emplace_back(std::move(all_infos[i]), std::move(info));
            }
          } else {
            LOG(ERROR) << "Have wrong language pack info \"" << lang.second << "\" in the database";
          }

          continue;
        }

        auto names = split(lang.second, '\x00');
        auto &info = pack->custom_language_pack_infos_[lang.first];
        info.name = std::move(names.first);
        info.native_name = std::move(names.second);
      }
    }
    pack_it = database->language_packs_.emplace(language_pack, std::move(pack)).first;
  }
  LanguagePack *pack = pack_it->second.get();

  std::lock_guard<std::mutex> languages_lock(pack->mutex_);
  auto code_it = pack->languages_.find(language_code);
  if (code_it == pack->languages_.end()) {
    auto language = make_unique<Language>();
    if (!database->database_.empty()) {
      language->kv_
          .init_with_connection(database->database_.clone(), get_database_table_name(language_pack, language_code))
          .ensure();
      language->version_ = load_database_language_version(&language->kv_);
      language->key_count_ = load_database_language_key_count(&language->kv_);
    }
    code_it = pack->languages_.emplace(language_code, std::move(language)).first;
  }
  return code_it->second.get();
}

bool LanguagePackManager::language_has_string_unsafe(const Language *language, const string &key) {
  return language->ordinary_strings_.count(key) != 0 || language->pluralized_strings_.count(key) != 0 ||
         language->deleted_strings_.count(key) != 0;
}

bool LanguagePackManager::language_has_strings(Language *language, const vector<string> &keys) {
  if (language == nullptr) {
    return false;
  }

  std::lock_guard<std::mutex> lock(language->mutex_);
  if (language->is_full_) {
    return true;
  }
  if (keys.empty()) {
    return false;  // language is already checked to be not full
  }
  for (auto &key : keys) {
    if (!language_has_string_unsafe(language, key)) {
      return false;
    }
  }
  return true;
}

void LanguagePackManager::load_language_string_unsafe(Language *language, const string &key, string &value) {
  CHECK(is_valid_key(key));
  if (value.empty() || value == "3") {
    if (!language->is_full_) {
      language->deleted_strings_.insert(key);
    }
    return;
  }

  if (value[0] == '1') {
    language->ordinary_strings_.emplace(key, value.substr(1));
    return;
  }

  CHECK(value[0] == '2');
  auto all = full_split(Slice(value).substr(1), '\x00');
  CHECK(all.size() == 6);
  language->pluralized_strings_.emplace(
      key, PluralizedString{all[0].str(), all[1].str(), all[2].str(), all[3].str(), all[4].str(), all[5].str()});
}

bool LanguagePackManager::load_language_strings(LanguageDatabase *database, Language *language,
                                                const vector<string> &keys) {
  if (language == nullptr) {
    return false;
  }

  std::lock_guard<std::mutex> database_lock(database->mutex_);
  std::lock_guard<std::mutex> language_lock(language->mutex_);
  if (language->is_full_) {
    LOG(DEBUG) << "The language pack is already full in memory";
    return true;
  }
  if (language->kv_.empty()) {
    LOG(DEBUG) << "The language pack has no database";
    return false;
  }
  LOG(DEBUG) << "Begin to load a language pack from database";
  if (keys.empty()) {
    if (language->version_ == -1 && language->was_loaded_full_) {
      LOG(DEBUG) << "The language pack has already been loaded";
      return false;
    }

    auto all_strings = language->kv_.get_all();
    for (auto &str : all_strings) {
      if (str.first[0] == '!') {
        continue;
      }

      if (!language_has_string_unsafe(language, str.first)) {
        LOG(DEBUG) << "Load string with key " << str.first << " from database";
        load_language_string_unsafe(language, str.first, str.second);
      }
    }
    language->was_loaded_full_ = true;

    if (language->version_ == -1) {
      return false;
    }

    language->is_full_ = true;
    language->deleted_strings_.clear();
    return true;
  }

  bool have_all = true;
  for (auto &key : keys) {
    if (!language_has_string_unsafe(language, key)) {
      auto value = language->kv_.get(key);
      if (value.empty()) {
        if (language->version_ == -1) {
          LOG(DEBUG) << "Have no string with key " << key << " in the database";
          have_all = false;
          continue;
        }

        // have full language in the database, so this string is just deleted
      }
      LOG(DEBUG) << "Load string with key " << key << " from database";
      load_language_string_unsafe(language, key, value);
    }
  }
  return have_all;
}

td_api::object_ptr<td_api::LanguagePackStringValue> LanguagePackManager::get_language_pack_string_value_object(
    const string &value) {
  return td_api::make_object<td_api::languagePackStringValueOrdinary>(value);
}

td_api::object_ptr<td_api::LanguagePackStringValue> LanguagePackManager::get_language_pack_string_value_object(
    const PluralizedString &value) {
  return td_api::make_object<td_api::languagePackStringValuePluralized>(
      value.zero_value_, value.one_value_, value.two_value_, value.few_value_, value.many_value_, value.other_value_);
}

td_api::object_ptr<td_api::LanguagePackStringValue> LanguagePackManager::get_language_pack_string_value_object() {
  return td_api::make_object<td_api::languagePackStringValueDeleted>();
}

td_api::object_ptr<td_api::languagePackString> LanguagePackManager::get_language_pack_string_object(
    const std::pair<string, string> &str) {
  return td_api::make_object<td_api::languagePackString>(str.first, get_language_pack_string_value_object(str.second));
}

td_api::object_ptr<td_api::languagePackString> LanguagePackManager::get_language_pack_string_object(
    const std::pair<string, PluralizedString> &str) {
  return td_api::make_object<td_api::languagePackString>(str.first, get_language_pack_string_value_object(str.second));
}

td_api::object_ptr<td_api::languagePackString> LanguagePackManager::get_language_pack_string_object(const string &str) {
  return td_api::make_object<td_api::languagePackString>(str, get_language_pack_string_value_object());
}

td_api::object_ptr<td_api::LanguagePackStringValue> LanguagePackManager::get_language_pack_string_value_object(
    const Language *language, const string &key) {
  CHECK(language != nullptr);
  auto ordinary_it = language->ordinary_strings_.find(key);
  if (ordinary_it != language->ordinary_strings_.end()) {
    return get_language_pack_string_value_object(ordinary_it->second);
  }
  auto pluralized_it = language->pluralized_strings_.find(key);
  if (pluralized_it != language->pluralized_strings_.end()) {
    return get_language_pack_string_value_object(pluralized_it->second);
  }
  LOG_IF(ERROR, !language->is_full_ && language->deleted_strings_.count(key) == 0) << "Have no string for key " << key;
  return get_language_pack_string_value_object();
}

td_api::object_ptr<td_api::languagePackString> LanguagePackManager::get_language_pack_string_object(
    const Language *language, const string &key) {
  return td_api::make_object<td_api::languagePackString>(key, get_language_pack_string_value_object(language, key));
}

td_api::object_ptr<td_api::languagePackStrings> LanguagePackManager::get_language_pack_strings_object(
    Language *language, const vector<string> &keys) {
  CHECK(language != nullptr);

  std::lock_guard<std::mutex> lock(language->mutex_);
  vector<td_api::object_ptr<td_api::languagePackString>> strings;
  if (keys.empty()) {
    for (auto &str : language->ordinary_strings_) {
      strings.push_back(get_language_pack_string_object(str));
    }
    for (auto &str : language->pluralized_strings_) {
      strings.push_back(get_language_pack_string_object(str));
    }
  } else {
    for (auto &key : keys) {
      strings.push_back(get_language_pack_string_object(language, key));
    }
  }

  return td_api::make_object<td_api::languagePackStrings>(std::move(strings));
}

void LanguagePackManager::get_languages(bool only_local,
                                        Promise<td_api::object_ptr<td_api::localizationTargetInfo>> promise) {
  if (language_pack_.empty()) {
    return promise.set_error(Status::Error(400, "Option \"localization_target\" needs to be set first"));
  }

  if (only_local) {
    return on_get_languages(vector<tl_object_ptr<telegram_api::langPackLanguage>>(), language_pack_, true,
                            std::move(promise));
  }

  auto request_promise = PromiseCreator::lambda([actor_id = actor_id(this), language_pack = language_pack_,
                                                 promise = std::move(promise)](Result<NetQueryPtr> r_query) mutable {
    auto r_result = fetch_result<telegram_api::langpack_getLanguages>(std::move(r_query));
    if (r_result.is_error()) {
      return promise.set_error(r_result.move_as_error());
    }

    send_closure(actor_id, &LanguagePackManager::on_get_languages, r_result.move_as_ok(), std::move(language_pack),
                 false, std::move(promise));
  });
  send_with_promise(G()->net_query_creator().create(create_storer(telegram_api::langpack_getLanguages(language_pack_))),
                    std::move(request_promise));
}

void LanguagePackManager::on_get_languages(vector<tl_object_ptr<telegram_api::langPackLanguage>> languages,
                                           string language_pack, bool only_local,
                                           Promise<td_api::object_ptr<td_api::localizationTargetInfo>> promise) {
  auto results = td_api::make_object<td_api::localizationTargetInfo>();

  {
    std::lock_guard<std::mutex> packs_lock(database_->mutex_);
    auto pack_it = database_->language_packs_.find(language_pack);
    if (pack_it != database_->language_packs_.end()) {
      LanguagePack *pack = pack_it->second.get();
      for (auto &info : pack->custom_language_pack_infos_) {
        results->language_packs_.push_back(
            make_tl_object<td_api::languagePackInfo>(info.first, info.second.name, info.second.native_name, 0));
      }
      if (only_local) {
        for (auto &info : pack->server_language_pack_infos_) {
          results->language_packs_.push_back(
              make_tl_object<td_api::languagePackInfo>(info.first, info.second.name, info.second.native_name, 0));
        }
      }
    }
  }

  vector<std::pair<string, LanguageInfo>> all_server_infos;
  for (auto &language : languages) {
    if (!check_language_code_name(language->lang_code_)) {
      LOG(ERROR) << "Receive unsupported language pack ID " << language->lang_code_ << " from server";
      continue;
    }

    results->language_packs_.push_back(
        make_tl_object<td_api::languagePackInfo>(language->lang_code_, language->name_, language->native_name_, 0));

    LanguageInfo info;
    info.name = std::move(language->name_);
    info.native_name = std::move(language->native_name_);
    all_server_infos.emplace_back(std::move(language->lang_code_), std::move(info));
  }

  for (auto &language_info : results->language_packs_) {
    auto language = add_language(database_, language_pack, language_info->id_);
    language_info->local_string_count_ = language->key_count_;
  }

  if (!only_local) {
    std::lock_guard<std::mutex> packs_lock(database_->mutex_);
    auto pack_it = database_->language_packs_.find(language_pack);
    if (pack_it != database_->language_packs_.end()) {
      LanguagePack *pack = pack_it->second.get();
      if (pack->server_language_pack_infos_ != all_server_infos) {
        pack->server_language_pack_infos_ = std::move(all_server_infos);

        if (!pack->pack_kv_.empty()) {
          vector<string> all_strings;
          all_strings.reserve(3 * pack->server_language_pack_infos_.size());
          for (auto &info : pack->server_language_pack_infos_) {
            all_strings.push_back(info.first);
            all_strings.push_back(info.second.name);
            all_strings.push_back(info.second.native_name);
          }

          pack->pack_kv_.set("!server", implode(all_strings, '\x00'));
        }
      }
    }
  }
  promise.set_value(std::move(results));
}

void LanguagePackManager::get_language_pack_strings(string language_code, vector<string> keys,
                                                    Promise<td_api::object_ptr<td_api::languagePackStrings>> promise) {
  if (!check_language_code_name(language_code) || language_code.empty()) {
    return promise.set_error(Status::Error(400, "Language pack ID is invalid"));
  }
  if (language_pack_.empty()) {
    return promise.set_error(Status::Error(400, "Option \"localization_target\" needs to be set first"));
  }
  for (auto &key : keys) {
    if (!is_valid_key(key)) {
      return promise.set_error(Status::Error(400, "Invalid key name"));
    }
  }

  Language *language = add_language(database_, language_pack_, language_code);
  if (language_has_strings(language, keys)) {
    return promise.set_value(get_language_pack_strings_object(language, keys));
  }
  if (load_language_strings(database_, language, keys)) {
    return promise.set_value(get_language_pack_strings_object(language, keys));
  }

  if (is_custom_language_code(language_code)) {
    return promise.set_error(Status::Error(400, "Custom language pack not found"));
  }

  if (keys.empty()) {
    auto &queries = get_all_language_pack_strings_queries_[language_pack_][language_code].queries_;
    queries.push_back(std::move(promise));
    if (queries.size() != 1) {
      // send request only once
      return;
    }

    auto result_promise =
        PromiseCreator::lambda([actor_id = actor_id(this), language_pack = language_pack_, language_code](
                                   Result<td_api::object_ptr<td_api::languagePackStrings>> r_strings) mutable {
          send_closure(actor_id, &LanguagePackManager::on_get_all_language_pack_strings, language_pack, language_code,
                       std::move(r_strings));
        });
    auto request_promise =
        PromiseCreator::lambda([actor_id = actor_id(this), language_pack = language_pack_, language_code,
                                promise = std::move(result_promise)](Result<NetQueryPtr> r_query) mutable {
          auto r_result = fetch_result<telegram_api::langpack_getLangPack>(std::move(r_query));
          if (r_result.is_error()) {
            return promise.set_error(r_result.move_as_error());
          }

          auto result = r_result.move_as_ok();
          LOG(INFO) << "Receive language pack " << result->lang_code_ << " from version " << result->from_version_
                    << " with version " << result->version_ << " of size " << result->strings_.size();
          LOG_IF(ERROR, result->lang_code_ != language_code)
              << "Receive strings for " << result->lang_code_ << " instead of " << language_code;
          LOG_IF(ERROR, result->from_version_ != 0) << "Receive language pack from version " << result->from_version_;
          send_closure(actor_id, &LanguagePackManager::on_get_language_pack_strings, std::move(language_pack),
                       std::move(language_code), result->version_, false, vector<string>(), std::move(result->strings_),
                       std::move(promise));
        });
    send_with_promise(G()->net_query_creator().create(
                          create_storer(telegram_api::langpack_getLangPack(language_pack_, language_code))),
                      std::move(request_promise));
  } else {
    auto request_promise =
        PromiseCreator::lambda([actor_id = actor_id(this), language_pack = language_pack_, language_code, keys,
                                promise = std::move(promise)](Result<NetQueryPtr> r_query) mutable {
          auto r_result = fetch_result<telegram_api::langpack_getStrings>(std::move(r_query));
          if (r_result.is_error()) {
            return promise.set_error(r_result.move_as_error());
          }

          send_closure(actor_id, &LanguagePackManager::on_get_language_pack_strings, std::move(language_pack),
                       std::move(language_code), -1, false, std::move(keys), r_result.move_as_ok(), std::move(promise));
        });
    send_with_promise(G()->net_query_creator().create(create_storer(
                          telegram_api::langpack_getStrings(language_pack_, language_code, std::move(keys)))),
                      std::move(request_promise));
  }
}

static td_api::object_ptr<td_api::LanguagePackStringValue> copy_language_pack_string_value(
    const td_api::LanguagePackStringValue *value) {
  switch (value->get_id()) {
    case td_api::languagePackStringValueOrdinary::ID: {
      auto old_value = static_cast<const td_api::languagePackStringValueOrdinary *>(value);
      return make_tl_object<td_api::languagePackStringValueOrdinary>(old_value->value_);
    }
    case td_api::languagePackStringValuePluralized::ID: {
      auto old_value = static_cast<const td_api::languagePackStringValuePluralized *>(value);
      return make_tl_object<td_api::languagePackStringValuePluralized>(
          std::move(old_value->zero_value_), std::move(old_value->one_value_), std::move(old_value->two_value_),
          std::move(old_value->few_value_), std::move(old_value->many_value_), std::move(old_value->other_value_));
    }
    case td_api::languagePackStringValueDeleted::ID:
      return make_tl_object<td_api::languagePackStringValueDeleted>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

void LanguagePackManager::on_get_all_language_pack_strings(
    string language_pack, string language_code, Result<td_api::object_ptr<td_api::languagePackStrings>> r_strings) {
  auto &queries = get_all_language_pack_strings_queries_[language_pack][language_code].queries_;
  auto promises = std::move(queries);
  CHECK(!promises.empty());
  auto it = get_all_language_pack_strings_queries_.find(language_pack);
  it->second.erase(language_code);
  if (it->second.empty()) {
    get_all_language_pack_strings_queries_.erase(it);
  }

  if (r_strings.is_error()) {
    for (auto &promise : promises) {
      promise.set_error(r_strings.error().clone());
    }
    return;
  }

  auto strings = r_strings.move_as_ok();
  size_t left_non_empty_promise_count = 0;
  for (auto &promise : promises) {
    if (promise) {
      left_non_empty_promise_count++;
    }
  }
  for (auto &promise : promises) {
    if (promise) {
      if (left_non_empty_promise_count == 1) {
        LOG(DEBUG) << "Set last non-empty promise";
        promise.set_value(std::move(strings));
      } else {
        LOG(DEBUG) << "Set non-empty promise";
        vector<td_api::object_ptr<td_api::languagePackString>> strings_copy;
        for (auto &result : strings->strings_) {
          CHECK(result != nullptr);
          strings_copy.push_back(td_api::make_object<td_api::languagePackString>(
              result->key_, copy_language_pack_string_value(result->value_.get())));
        }
        promise.set_value(td_api::make_object<td_api::languagePackStrings>(std::move(strings_copy)));
      }
      left_non_empty_promise_count--;
    } else {
      LOG(DEBUG) << "Set empty promise";
      promise.set_value(nullptr);
    }
  }
  CHECK(left_non_empty_promise_count == 0);
}

td_api::object_ptr<td_api::Object> LanguagePackManager::get_language_pack_string(const string &database_path,
                                                                                 const string &language_pack,
                                                                                 const string &language_code,
                                                                                 const string &key) {
  if (!check_language_pack_name(language_pack) || language_pack.empty()) {
    return td_api::make_object<td_api::error>(400, "Localization target is invalid");
  }
  if (!check_language_code_name(language_code) || language_code.empty()) {
    return td_api::make_object<td_api::error>(400, "Language pack ID is invalid");
  }
  if (!is_valid_key(key)) {
    return td_api::make_object<td_api::error>(400, "Key is invalid");
  }

  std::unique_lock<std::mutex> language_databases_lock(language_database_mutex_);
  LanguageDatabase *database = add_language_database(database_path);
  CHECK(database != nullptr);
  language_databases_lock.unlock();

  Language *language = add_language(database, language_pack, language_code);
  vector<string> keys{key};
  if (language_has_strings(language, keys)) {
    std::lock_guard<std::mutex> lock(language->mutex_);
    return get_language_pack_string_value_object(language, key);
  }
  if (load_language_strings(database, language, keys)) {
    std::lock_guard<std::mutex> lock(language->mutex_);
    return get_language_pack_string_value_object(language, key);
  }
  return td_api::make_object<td_api::error>(404, "Not Found");
}

bool LanguagePackManager::is_valid_key(Slice key) {
  for (auto c : key) {
    if (!is_alnum(c) && c != '_' && c != '.' && c != '-') {
      return false;
    }
  }
  return !key.empty();
}

void LanguagePackManager::save_strings_to_database(SqliteKeyValue *kv, int32 new_version, bool new_is_full,
                                                   int32 new_key_count, vector<std::pair<string, string>> strings) {
  LOG(DEBUG) << "Save to database a language pack with new version " << new_version << " and " << strings.size()
             << " new strings";
  if (new_version == -1 && strings.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(database_->mutex_);
  CHECK(kv != nullptr);
  if (kv->empty()) {
    LOG(DEBUG) << "There is no associated database key-value";
    return;
  }
  auto old_version = load_database_language_version(kv);
  if (old_version > new_version || (old_version == new_version && strings.empty())) {
    LOG(DEBUG) << "Language pack version doesn't increased from " << old_version;
    return;
  }

  kv->begin_transaction().ensure();
  for (auto str : strings) {
    if (!is_valid_key(str.first)) {
      LOG(ERROR) << "Have invalid key \"" << str.first << '"';
      continue;
    }

    if (new_is_full && str.second == "3") {
      kv->erase(str.first);
    } else {
      kv->set(str.first, str.second);
    }
    LOG(DEBUG) << "Save language pack string with key " << str.first << " to database";
  }
  if (old_version != new_version) {
    LOG(DEBUG) << "Set language pack version in database to " << new_version;
    kv->set("!version", to_string(new_version));
  }
  if (new_key_count != -1) {
    LOG(DEBUG) << "Set language pack key count in database to " << new_key_count;
    kv->set("!key_count", to_string(new_key_count));
  }
  kv->commit_transaction().ensure();
}

void LanguagePackManager::on_get_language_pack_strings(
    string language_pack, string language_code, int32 version, bool is_diff, vector<string> keys,
    vector<tl_object_ptr<telegram_api::LangPackString>> results,
    Promise<td_api::object_ptr<td_api::languagePackStrings>> promise) {
  Language *language = get_language(database_, language_pack, language_code);
  bool is_version_changed = false;
  int32 new_database_version = -1;
  int32 new_key_count = -1;
  bool new_is_full = false;
  vector<std::pair<string, string>> database_strings;
  if (language == nullptr || language->version_ < version || !keys.empty()) {
    if (language == nullptr) {
      language = add_language(database_, language_pack, language_code);
      CHECK(language != nullptr);
    }
    load_language_strings(database_, language, keys);

    std::lock_guard<std::mutex> lock(language->mutex_);
    int32 key_count_delta = 0;
    if (language->version_ < version || !keys.empty()) {
      vector<td_api::object_ptr<td_api::languagePackString>> strings;
      if (language->version_ < version && !(is_diff && language->version_ == -1)) {
        LOG(INFO) << "Set language pack " << language_code << " version to " << version;
        language->version_ = version;
        new_database_version = version;
        is_version_changed = true;
      }

      for (auto &result : results) {
        CHECK(result != nullptr);
        switch (result->get_id()) {
          case telegram_api::langPackString::ID: {
            auto str = static_cast<telegram_api::langPackString *>(result.get());
            auto it = language->ordinary_strings_.find(str->key_);
            if (it == language->ordinary_strings_.end()) {
              key_count_delta++;
              it = language->ordinary_strings_.emplace(str->key_, std::move(str->value_)).first;
            } else {
              it->second = std::move(str->value_);
            }
            key_count_delta -= static_cast<int32>(language->pluralized_strings_.erase(str->key_));
            language->deleted_strings_.erase(str->key_);
            if (is_diff) {
              strings.push_back(get_language_pack_string_object(*it));
            }
            database_strings.emplace_back(str->key_, PSTRING() << '1' << it->second);
            break;
          }
          case telegram_api::langPackStringPluralized::ID: {
            auto str = static_cast<const telegram_api::langPackStringPluralized *>(result.get());
            PluralizedString value{std::move(str->zero_value_), std::move(str->one_value_),
                                   std::move(str->two_value_),  std::move(str->few_value_),
                                   std::move(str->many_value_), std::move(str->other_value_)};
            auto it = language->pluralized_strings_.find(str->key_);
            if (it == language->pluralized_strings_.end()) {
              key_count_delta++;
              it = language->pluralized_strings_.emplace(str->key_, std::move(value)).first;
            } else {
              it->second = std::move(value);
            }
            key_count_delta -= static_cast<int32>(language->ordinary_strings_.erase(str->key_));
            language->deleted_strings_.erase(str->key_);
            if (is_diff) {
              strings.push_back(get_language_pack_string_object(*it));
            }
            database_strings.emplace_back(
                str->key_, PSTRING() << '2' << it->second.zero_value_ << '\x00' << it->second.one_value_ << '\x00'
                                     << it->second.two_value_ << '\x00' << it->second.few_value_ << '\x00'
                                     << it->second.many_value_ << '\x00' << it->second.other_value_);
            break;
          }
          case telegram_api::langPackStringDeleted::ID: {
            auto str = static_cast<const telegram_api::langPackStringDeleted *>(result.get());
            key_count_delta -= static_cast<int32>(language->ordinary_strings_.erase(str->key_));
            key_count_delta -= static_cast<int32>(language->pluralized_strings_.erase(str->key_));
            language->deleted_strings_.insert(std::move(str->key_));
            if (is_diff) {
              strings.push_back(get_language_pack_string_object(str->key_));
            }
            database_strings.emplace_back(str->key_, "3");
            break;
          }
          default:
            UNREACHABLE();
            break;
        }
      }
      if (!language->is_full_) {
        for (auto &key : keys) {
          if (!language_has_string_unsafe(language, key)) {
            LOG(ERROR) << "Doesn't receive key " << key << " from server";
            language->deleted_strings_.insert(key);
            if (is_diff) {
              strings.push_back(get_language_pack_string_object(key));
            }
            database_strings.emplace_back(key, "3");
          }
        }
      }

      if (key_count_delta != 0) {
        new_key_count = language->key_count_ + key_count_delta;
        language->key_count_ = new_key_count;
      }

      if (is_diff) {
        send_closure(
            G()->td(), &Td::send_update,
            td_api::make_object<td_api::updateLanguagePackStrings>(language_pack, language_code, std::move(strings)));
      }

      if (keys.empty() && !is_diff) {
        CHECK(new_database_version >= 0);
        language->is_full_ = true;
        language->deleted_strings_.clear();
      }
      new_is_full = language->is_full_;
    }
  }
  if (is_custom_language_code(language_code) && new_database_version == -1) {
    new_database_version = 1;
  }

  save_strings_to_database(&language->kv_, new_database_version, new_is_full, new_key_count,
                           std::move(database_strings));

  if (is_diff) {
    CHECK(language != nullptr);
    std::lock_guard<std::mutex> lock(language->mutex_);
    if (language->has_get_difference_query_) {
      language->has_get_difference_query_ = false;
      is_version_changed = true;
    }
  }
  if (is_version_changed && language_pack == language_pack_ && language_code == language_code_) {
    send_closure_later(actor_id(this), &LanguagePackManager::on_language_pack_version_changed, -1);
  }

  if (promise) {
    promise.set_value(get_language_pack_strings_object(language, keys));
  }
}

void LanguagePackManager::on_failed_get_difference(string language_pack, string language_code) {
  Language *language = get_language(database_, language_pack, language_code);
  CHECK(language != nullptr);
  std::lock_guard<std::mutex> lock(language->mutex_);
  if (language->has_get_difference_query_) {
    language->has_get_difference_query_ = false;
    if (language_pack == language_pack_ && language_code == language_code_) {
      send_closure_later(actor_id(this), &LanguagePackManager::on_language_pack_version_changed, -1);
    }
  }
}

Result<tl_object_ptr<telegram_api::LangPackString>> LanguagePackManager::convert_to_telegram_api(
    tl_object_ptr<td_api::languagePackString> &&str) {
  if (str == nullptr) {
    return Status::Error(400, "Language pack strings must not be null");
  }

  string key = std::move(str->key_);
  if (!is_valid_key(key)) {
    return Status::Error(400, "Key is invalid");
  }

  if (str->value_ == nullptr) {
    return make_tl_object<telegram_api::langPackStringDeleted>(std::move(key));
  }
  switch (str->value_->get_id()) {
    case td_api::languagePackStringValueOrdinary::ID: {
      auto value = static_cast<td_api::languagePackStringValueOrdinary *>(str->value_.get());
      if (!clean_input_string(value->value_)) {
        return Status::Error(400, "Language pack string value must be encoded in UTF-8");
      }
      return make_tl_object<telegram_api::langPackString>(std::move(key), std::move(value->value_));
    }
    case td_api::languagePackStringValuePluralized::ID: {
      auto value = static_cast<td_api::languagePackStringValuePluralized *>(str->value_.get());
      if (!clean_input_string(value->zero_value_) || !clean_input_string(value->one_value_) ||
          !clean_input_string(value->two_value_) || !clean_input_string(value->few_value_) ||
          !clean_input_string(value->many_value_) || !clean_input_string(value->other_value_)) {
        return Status::Error(400, "Language pack string value must be encoded in UTF-8");
      }
      return make_tl_object<telegram_api::langPackStringPluralized>(
          31, std::move(key), std::move(value->zero_value_), std::move(value->one_value_), std::move(value->two_value_),
          std::move(value->few_value_), std::move(value->many_value_), std::move(value->other_value_));
    }
    case td_api::languagePackStringValueDeleted::ID:
      // there is no reason to save deleted strings in a custom language pack to database
      return make_tl_object<telegram_api::langPackStringDeleted>(std::move(key));
    default:
      UNREACHABLE();
      return nullptr;
  }
}

void LanguagePackManager::set_custom_language(string language_code, string language_name, string language_native_name,
                                              vector<tl_object_ptr<td_api::languagePackString>> strings,
                                              Promise<Unit> &&promise) {
  if (language_pack_.empty()) {
    return promise.set_error(Status::Error(400, "Option \"localization_target\" needs to be set first"));
  }
  if (!check_language_code_name(language_code)) {
    return promise.set_error(Status::Error(400, "Language pack ID must contain only letters, digits and hyphen"));
  }
  if (!is_custom_language_code(language_code)) {
    return promise.set_error(Status::Error(400, "Custom language pack ID must begin with 'X'"));
  }

  vector<tl_object_ptr<telegram_api::LangPackString>> server_strings;
  for (auto &str : strings) {
    auto r_result = convert_to_telegram_api(std::move(str));
    if (r_result.is_error()) {
      return promise.set_error(r_result.move_as_error());
    }
    server_strings.push_back(r_result.move_as_ok());
  }

  // TODO atomic replace
  do_delete_language(language_code).ensure();
  on_get_language_pack_strings(language_pack_, language_code, 1, false, vector<string>(), std::move(server_strings),
                               Auto());
  std::lock_guard<std::mutex> packs_lock(database_->mutex_);
  auto pack_it = database_->language_packs_.find(language_pack_);
  CHECK(pack_it != database_->language_packs_.end());
  LanguagePack *pack = pack_it->second.get();
  auto &info = pack->custom_language_pack_infos_[language_code];
  info.name = std::move(language_name);
  info.native_name = std::move(language_native_name);
  if (!pack->pack_kv_.empty()) {
    pack->pack_kv_.set(language_code, PSLICE() << info.name << '\x00' << info.native_name);
  }

  promise.set_value(Unit());
}

void LanguagePackManager::edit_custom_language_info(string language_code, string language_name,
                                                    string language_native_name, Promise<Unit> &&promise) {
  if (language_pack_.empty()) {
    return promise.set_error(Status::Error(400, "Option \"localization_target\" needs to be set first"));
  }
  if (!check_language_code_name(language_code)) {
    return promise.set_error(Status::Error(400, "Language pack ID must contain only letters, digits and hyphen"));
  }
  if (!is_custom_language_code(language_code)) {
    return promise.set_error(Status::Error(400, "Custom language pack ID must begin with 'X'"));
  }

  std::lock_guard<std::mutex> packs_lock(database_->mutex_);
  auto pack_it = database_->language_packs_.find(language_pack_);
  CHECK(pack_it != database_->language_packs_.end());
  LanguagePack *pack = pack_it->second.get();
  auto language_info_it = pack->custom_language_pack_infos_.find(language_code);
  if (language_info_it == pack->custom_language_pack_infos_.end()) {
    return promise.set_error(Status::Error(400, "Custom language pack is not found"));
  }
  auto &info = language_info_it->second;
  info.name = std::move(language_name);
  info.native_name = std::move(language_native_name);
  if (!pack->pack_kv_.empty()) {
    pack->pack_kv_.set(language_code, PSLICE() << info.name << '\x00' << info.native_name);
  }

  promise.set_value(Unit());
}

void LanguagePackManager::set_custom_language_string(string language_code,
                                                     tl_object_ptr<td_api::languagePackString> str,
                                                     Promise<Unit> &&promise) {
  if (language_pack_.empty()) {
    return promise.set_error(Status::Error(400, "Option \"localization_target\" needs to be set first"));
  }
  if (!check_language_code_name(language_code)) {
    return promise.set_error(Status::Error(400, "Language pack ID must contain only letters, digits and hyphen"));
  }
  if (!is_custom_language_code(language_code)) {
    return promise.set_error(Status::Error(400, "Custom language pack ID must begin with 'X'"));
  }

  if (get_language(database_, language_pack_, language_code) == nullptr) {
    return promise.set_error(Status::Error(400, "Custom language pack not found"));
  }
  if (str == nullptr) {
    return promise.set_error(Status::Error(400, "Language pack strings must not be null"));
  }

  vector<string> keys{str->key_};

  auto r_str = convert_to_telegram_api(std::move(str));
  if (r_str.is_error()) {
    return promise.set_error(r_str.move_as_error());
  }

  vector<tl_object_ptr<telegram_api::LangPackString>> server_strings;
  server_strings.push_back(r_str.move_as_ok());

  on_get_language_pack_strings(language_pack_, language_code, 1, true, std::move(keys), std::move(server_strings),
                               Auto());
  promise.set_value(Unit());
}

void LanguagePackManager::delete_language(string language_code, Promise<Unit> &&promise) {
  if (language_pack_.empty()) {
    return promise.set_error(Status::Error(400, "Option \"localization_target\" needs to be set first"));
  }
  if (!check_language_code_name(language_code)) {
    return promise.set_error(Status::Error(400, "Language pack ID is invalid"));
  }
  if (language_code.empty()) {
    return promise.set_error(Status::Error(400, "Language pack ID is empty"));
  }
  if (language_code_ == language_code) {
    return promise.set_error(Status::Error(400, "Currently used language pack can't be deleted"));
  }

  auto status = do_delete_language(language_code);
  if (status.is_error()) {
    promise.set_error(std::move(status));
  } else {
    promise.set_value(Unit());
  }
}

Status LanguagePackManager::do_delete_language(string language_code) {
  add_language(database_, language_pack_, language_code);

  std::lock_guard<std::mutex> packs_lock(database_->mutex_);
  auto pack_it = database_->language_packs_.find(language_pack_);
  CHECK(pack_it != database_->language_packs_.end());
  LanguagePack *pack = pack_it->second.get();

  std::lock_guard<std::mutex> languages_lock(pack->mutex_);
  auto code_it = pack->languages_.find(language_code);
  CHECK(code_it != pack->languages_.end());
  auto language = code_it->second.get();
  if (language->has_get_difference_query_) {
    return Status::Error(400, "Language pack can't be deleted now, try again later");
  }
  if (!language->kv_.empty()) {
    language->kv_.drop().ignore();
    CHECK(language->kv_.empty());
    CHECK(!database_->database_.empty());
    language->kv_
        .init_with_connection(database_->database_.clone(), get_database_table_name(language_pack_, language_code))
        .ensure();
  }
  std::lock_guard<std::mutex> language_lock(language->mutex_);
  language->version_ = -1;
  language->key_count_ = load_database_language_key_count(&language->kv_);
  language->is_full_ = false;
  language->ordinary_strings_.clear();
  language->pluralized_strings_.clear();
  language->deleted_strings_.clear();

  if (is_custom_language_code(language_code)) {
    if (!pack->pack_kv_.empty()) {
      pack->pack_kv_.erase(language_code);
    }
    pack->custom_language_pack_infos_.erase(language_code);
  }

  return Status::OK();
}

void LanguagePackManager::on_result(NetQueryPtr query) {
  auto token = get_link_token();
  container_.extract(token).set_value(std::move(query));
}

void LanguagePackManager::send_with_promise(NetQueryPtr query, Promise<NetQueryPtr> promise) {
  auto id = container_.create(std::move(promise));
  G()->net_query_dispatcher().dispatch_with_callback(std::move(query), actor_shared(this, id));
}

void LanguagePackManager::hangup() {
  container_.for_each(
      [](auto id, Promise<NetQueryPtr> &promise) { promise.set_error(Status::Error(500, "Request aborted")); });
  stop();
}

int32 LanguagePackManager::manager_count_ = 0;
std::mutex LanguagePackManager::language_database_mutex_;
std::unordered_map<string, std::unique_ptr<LanguagePackManager::LanguageDatabase>>
    LanguagePackManager::language_databases_;

}  // namespace td
