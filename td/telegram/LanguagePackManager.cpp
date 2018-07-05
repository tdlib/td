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
#include "td/telegram/net/NetQueryDispatcher.h"

#include "td/utils/logging.h"
#include "td/utils/misc.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

namespace td {

void LanguagePackManager::start_up() {
  language_pack_ = G()->shared_config().get_option_string("language_pack");
  language_code_ = G()->shared_config().get_option_string("language_code");
  language_pack_version_ = G()->shared_config().get_option_integer("language_pack_version", -1);
  LOG(INFO) << "Use language pack " << language_pack_ << " with language " << language_code_ << " of version "
            << language_pack_version_;
}

void LanguagePackManager::on_language_pack_changed() {
  auto new_language_pack = G()->shared_config().get_option_string("language_pack");
  if (new_language_pack == language_pack_) {
    return;
  }

  language_pack_ = std::move(new_language_pack);
  inc_generation();
}

void LanguagePackManager::on_language_code_changed() {
  auto new_language_code = G()->shared_config().get_option_string("language_code");
  if (new_language_code == language_code_) {
    return;
  }

  language_code_ = std::move(new_language_code);
  inc_generation();
}

void LanguagePackManager::on_language_pack_version_changed() {
  auto new_language_pack_version = G()->shared_config().get_option_integer("language_pack_version");
  if (new_language_pack_version == language_pack_version_) {
    return;
  }
  if (language_pack_version_ == -1) {
    return;
  }

  // TODO update language pack
  language_pack_version_ = new_language_pack_version;
}

void LanguagePackManager::inc_generation() {
  generation_++;
  G()->shared_config().set_option_empty("language_pack_version");
  language_pack_version_ = -1;
}

LanguagePackManager::Language *LanguagePackManager::get_language(const string &language_pack,
                                                                 const string &language_code) {
  std::unique_lock<std::mutex> lock(language_packs_mutex_);
  auto it = language_packs_.find(language_pack);
  if (it == language_packs_.end()) {
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

LanguagePackManager::Language *LanguagePackManager::add_language(const string &language_pack,
                                                                 const string &language_code) {
  std::lock_guard<std::mutex> packs_lock(language_packs_mutex_);
  auto pack_it = language_packs_.find(language_pack);
  if (pack_it == language_packs_.end()) {
    pack_it = language_packs_.emplace(language_pack, make_unique<LanguagePack>()).first;
  }
  LanguagePack *pack = pack_it->second.get();

  std::lock_guard<std::mutex> languages_lock(pack->mutex_);
  auto code_it = pack->languages_.find(language_code);
  if (code_it == pack->languages_.end()) {
    code_it = pack->languages_.emplace(language_code, make_unique<Language>()).first;
  }
  return code_it->second.get();
}

bool LanguagePackManager::language_has_string_unsafe(Language *language, const string &key) {
  return language->ordinary_strings_.count(key) != 0 || language->pluralized_strings_.count(key) != 0 ||
         language->deleted_strings_.count(key) != 0;
}

bool LanguagePackManager::language_has_strings(Language *language, const vector<string> &keys) {
  if (language == nullptr) {
    return false;
  }

  std::lock_guard<std::mutex> lock(language->mutex_);
  if (keys.empty()) {
    return language->version_ != -1;
  }
  for (auto &key : keys) {
    if (!language_has_string_unsafe(language, key)) {
      return false;
    }
  }
  return true;
}

td_api::object_ptr<td_api::LanguagePackString> LanguagePackManager::get_language_pack_string_object(
    const std::pair<string, string> &str) {
  return td_api::make_object<td_api::languagePackStringValue>(str.first, str.second);
}

td_api::object_ptr<td_api::LanguagePackString> LanguagePackManager::get_language_pack_string_object(
    const std::pair<string, PluralizedString> &str) {
  return td_api::make_object<td_api::languagePackStringPluralized>(
      str.first, str.second.zero_value_, str.second.one_value_, str.second.two_value_, str.second.few_value_,
      str.second.many_value_, str.second.other_value_);
}

td_api::object_ptr<td_api::LanguagePackString> LanguagePackManager::get_language_pack_string_object(const string &str) {
  return td_api::make_object<td_api::languagePackStringDeleted>(str);
}

td_api::object_ptr<td_api::languagePackStrings> LanguagePackManager::get_language_pack_strings_object(
    Language *language, const vector<string> &keys) {
  CHECK(language != nullptr);

  std::lock_guard<std::mutex> lock(language->mutex_);
  vector<td_api::object_ptr<td_api::LanguagePackString>> strings;
  if (keys.empty()) {
    for (auto &str : language->ordinary_strings_) {
      strings.push_back(get_language_pack_string_object(str));
    }
    for (auto &str : language->pluralized_strings_) {
      strings.push_back(get_language_pack_string_object(str));
    }
  } else {
    for (auto &key : keys) {
      auto ordinary_it = language->ordinary_strings_.find(key);
      if (ordinary_it != language->ordinary_strings_.end()) {
        strings.push_back(get_language_pack_string_object(*ordinary_it));
        continue;
      }
      auto pluralized_it = language->pluralized_strings_.find(key);
      if (pluralized_it != language->pluralized_strings_.end()) {
        strings.push_back(get_language_pack_string_object(*pluralized_it));
        continue;
      }
      LOG_IF(ERROR, language->deleted_strings_.count(key) == 0) << "Have no string for key " << key;
      strings.push_back(get_language_pack_string_object(key));
    }
  }

  return td_api::make_object<td_api::languagePackStrings>(std::move(strings));
}

void LanguagePackManager::get_languages(Promise<td_api::object_ptr<td_api::languagePack>> promise) {
  auto request_promise = PromiseCreator::lambda([promise = std::move(promise)](Result<NetQueryPtr> r_query) mutable {
    auto r_result = fetch_result<telegram_api::langpack_getLanguages>(std::move(r_query));
    if (r_result.is_error()) {
      return promise.set_error(r_result.move_as_error());
    }

    auto languages = r_result.move_as_ok();
    auto results = make_tl_object<td_api::languagePack>();
    results->languages_.reserve(languages.size());
    for (auto &language : languages) {
      results->languages_.push_back(
          make_tl_object<td_api::languageInfo>(language->lang_code_, language->name_, language->native_name_));
    }
    promise.set_value(std::move(results));
  });
  send_with_promise(G()->net_query_creator().create(create_storer(telegram_api::langpack_getLanguages())),
                    std::move(request_promise));
}

void LanguagePackManager::get_language_pack_strings(string language_code, vector<string> keys,
                                                    Promise<td_api::object_ptr<td_api::languagePackStrings>> promise) {
  Language *language = get_language(language_pack_, language_code);
  if (language_has_strings(language, keys)) {
    return promise.set_value(get_language_pack_strings_object(language, keys));
  }

  if (keys.empty()) {
    auto request_promise =
        PromiseCreator::lambda([actor_id = actor_id(this), language_pack = language_pack_, language_code,
                                promise = std::move(promise)](Result<NetQueryPtr> r_query) mutable {
          auto r_result = fetch_result<telegram_api::langpack_getLangPack>(std::move(r_query));
          if (r_result.is_error()) {
            return promise.set_error(r_result.move_as_error());
          }

          auto result = r_result.move_as_ok();
          LOG(INFO) << "Receive language pack for language " << result->lang_code_ << " from version "
                    << result->from_version_ << " with version " << result->version_ << " of size "
                    << result->strings_.size();
          LOG_IF(ERROR, result->lang_code_ != language_code)
              << "Receive strings for " << result->lang_code_ << " instead of " << language_code;
          LOG_IF(ERROR, result->from_version_ != 0) << "Receive lang pack from version " << result->from_version_;
          send_closure(actor_id, &LanguagePackManager::on_get_language_pack_strings, std::move(language_pack),
                       std::move(language_code), result->version_, vector<string>(), std::move(result->strings_),
                       std::move(promise));
        });
    send_with_promise(G()->net_query_creator().create(create_storer(telegram_api::langpack_getLangPack(language_code))),
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
                       std::move(language_code), -1, std::move(keys), r_result.move_as_ok(), std::move(promise));
        });
    send_with_promise(G()->net_query_creator().create(
                          create_storer(telegram_api::langpack_getStrings(language_code, std::move(keys)))),
                      std::move(request_promise));
  }
}

void LanguagePackManager::on_get_language_pack_strings(
    string language_pack, string language_code, int32 version, vector<string> keys,
    vector<tl_object_ptr<telegram_api::LangPackString>> results,
    Promise<td_api::object_ptr<td_api::languagePackStrings>> promise) {
  Language *language = get_language(language_pack, language_code);
  if (language == nullptr || (language->version_ < version || !keys.empty())) {
    if (language == nullptr) {
      language = add_language(language_pack, language_code);
      CHECK(language != nullptr);
    }
    std::lock_guard<std::mutex> lock(language->mutex_);
    if (language->version_ < version || !keys.empty()) {
      if (language->version_ < version) {
        language->version_ = version;
      }
      for (auto &result : results) {
        CHECK(result != nullptr);
        switch (result->get_id()) {
          case telegram_api::langPackString::ID: {
            auto str = static_cast<telegram_api::langPackString *>(result.get());
            language->ordinary_strings_[str->key_] = std::move(str->value_);
            language->pluralized_strings_.erase(str->key_);
            language->deleted_strings_.erase(str->key_);
            break;
          }
          case telegram_api::langPackStringPluralized::ID: {
            auto str = static_cast<const telegram_api::langPackStringPluralized *>(result.get());
            language->pluralized_strings_[str->key_] = PluralizedString{
                std::move(str->zero_value_), std::move(str->one_value_),  std::move(str->two_value_),
                std::move(str->few_value_),  std::move(str->many_value_), std::move(str->other_value_)};
            language->ordinary_strings_.erase(str->key_);
            language->deleted_strings_.erase(str->key_);
            break;
          }
          case telegram_api::langPackStringDeleted::ID: {
            auto str = static_cast<const telegram_api::langPackStringDeleted *>(result.get());
            language->ordinary_strings_.erase(str->key_);
            language->pluralized_strings_.erase(str->key_);
            language->deleted_strings_.insert(std::move(str->key_));
            break;
          }
          default:
            UNREACHABLE();
            break;
        }
      }
      for (auto &key : keys) {
        if (!language_has_string_unsafe(language, key)) {
          LOG(ERROR) << "Doesn't receive key " << key << " from server";
          language->deleted_strings_.insert(key);
        }
      }

      // TODO save language
    }
  }

  promise.set_value(get_language_pack_strings_object(language, keys));
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

std::mutex LanguagePackManager::language_packs_mutex_;
std::unordered_map<string, std::unique_ptr<LanguagePackManager::LanguagePack>> LanguagePackManager::language_packs_;

}  // namespace td
