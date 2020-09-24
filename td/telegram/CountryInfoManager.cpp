//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/CountryInfoManager.h"

#include "td/telegram/Global.h"
#include "td/telegram/LanguagePackManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"

namespace td {

class GetNearestDcQuery : public Td::ResultHandler {
  Promise<string> promise_;

 public:
  explicit GetNearestDcQuery(Promise<string> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create_unauth(telegram_api::help_getNearestDc()));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::help_getNearestDc>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    promise_.set_value(std::move(result->country_));
  }

  void on_error(uint64 id, Status status) override {
    if (!G()->is_expected_error(status) && status.message() != "BOT_METHOD_INVALID") {
      LOG(ERROR) << "GetNearestDc returned " << status;
    }
    promise_.set_error(std::move(status));
  }
};

class GetCountriesListQuery : public Td::ResultHandler {
  Promise<tl_object_ptr<telegram_api::help_CountriesList>> promise_;

 public:
  explicit GetCountriesListQuery(Promise<tl_object_ptr<telegram_api::help_CountriesList>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &language_code, int32 hash) {
    send_query(G()->net_query_creator().create_unauth(telegram_api::help_getCountriesList(language_code, hash)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::help_getCountriesList>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    promise_.set_value(result_ptr.move_as_ok());
  }

  void on_error(uint64 id, Status status) override {
    if (!G()->is_expected_error(status)) {
      LOG(ERROR) << "GetCountriesList returned " << status;
    }
    promise_.set_error(std::move(status));
  }
};

struct CountryInfoManager::CallingCodeInfo {
  string calling_code;
  vector<string> prefixes;
  vector<string> patterns;
};

struct CountryInfoManager::CountryInfo {
  string country_code;
  string default_name;
  string name;
  vector<CallingCodeInfo> calling_codes;
  bool is_hidden = false;

  td_api::object_ptr<td_api::countryInfo> get_country_info_object() const {
    return td_api::make_object<td_api::countryInfo>(
        country_code, name.empty() ? default_name : name, default_name, is_hidden,
        transform(calling_codes, [](const CallingCodeInfo &info) { return info.calling_code; }));
  }
};

struct CountryInfoManager::CountryList {
  vector<CountryInfo> countries_;
  int32 hash = 0;
  double next_reload_time = 0.0;

  td_api::object_ptr<td_api::countries> get_countries_object() const {
    return td_api::make_object<td_api::countries>(
        transform(countries_, [](const CountryInfo &info) { return info.get_country_info_object(); }));
  }
};

CountryInfoManager::CountryInfoManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

CountryInfoManager::~CountryInfoManager() = default;

void CountryInfoManager::tear_down() {
  parent_.reset();
}

string CountryInfoManager::get_main_language_code() {
  return to_lower(td_->language_pack_manager_->get_actor_unsafe()->get_main_language_code());
}

void CountryInfoManager::get_countries(Promise<td_api::object_ptr<td_api::countries>> &&promise) {
  do_get_countries(get_main_language_code(), false, std::move(promise));
}

void CountryInfoManager::do_get_countries(string language_code, bool is_recursive,
                                          Promise<td_api::object_ptr<td_api::countries>> &&promise) {
  if (is_recursive) {
    auto main_language_code = get_main_language_code();
    if (language_code != main_language_code) {
      language_code = std::move(main_language_code);
      is_recursive = false;
    }
  }
  auto list = get_country_list(language_code);
  if (list == nullptr) {
    if (is_recursive) {
      return promise.set_error(Status::Error(500, "Requested data is inaccessible"));
    }
    return load_country_list(language_code, 0,
                             PromiseCreator::lambda([actor_id = actor_id(this), language_code,
                                                     promise = std::move(promise)](Result<Unit> &&result) mutable {
                               if (result.is_error()) {
                                 return promise.set_error(result.move_as_error());
                               }
                               send_closure(actor_id, &CountryInfoManager::do_get_countries, std::move(language_code),
                                            true, std::move(promise));
                             }));
  }

  promise.set_value(list->get_countries_object());
}

void CountryInfoManager::get_phone_number_info(string phone_number_prefix,
                                               Promise<td_api::object_ptr<td_api::phoneNumberInfo>> &&promise) {
  td::remove_if(phone_number_prefix, [](char c) { return c < '0' || c > '9'; });
  if (phone_number_prefix.empty()) {
    return promise.set_value(td_api::make_object<td_api::phoneNumberInfo>(nullptr, string(), string()));
  }
  do_get_phone_number_info(std::move(phone_number_prefix), get_main_language_code(), false, std::move(promise));
}

void CountryInfoManager::do_get_phone_number_info(string phone_number_prefix, string language_code, bool is_recursive,
                                                  Promise<td_api::object_ptr<td_api::phoneNumberInfo>> &&promise) {
  if (is_recursive) {
    auto main_language_code = get_main_language_code();
    if (language_code != main_language_code) {
      language_code = std::move(main_language_code);
      is_recursive = false;
    }
  }
  auto list = get_country_list(language_code);
  if (list == nullptr) {
    if (is_recursive) {
      return promise.set_error(Status::Error(500, "Requested data is inaccessible"));
    }
    return load_country_list(language_code, 0,
                             PromiseCreator::lambda([actor_id = actor_id(this), phone_number_prefix, language_code,
                                                     promise = std::move(promise)](Result<Unit> &&result) mutable {
                               if (result.is_error()) {
                                 return promise.set_error(result.move_as_error());
                               }
                               send_closure(actor_id, &CountryInfoManager::do_get_phone_number_info,
                                            std::move(phone_number_prefix), std::move(language_code), true,
                                            std::move(promise));
                             }));
  }

  Slice phone_number = phone_number_prefix;
  const CountryInfo *best_country = nullptr;
  const CallingCodeInfo *best_calling_code = nullptr;
  size_t best_length = 0;
  bool is_prefix = false;
  for (auto &country : list->countries_) {
    for (auto &calling_code : country.calling_codes) {
      if (begins_with(phone_number, calling_code.calling_code)) {
        auto calling_code_size = calling_code.calling_code.size();
        for (auto &prefix : calling_code.prefixes) {
          if (begins_with(prefix, phone_number.substr(calling_code_size))) {
            is_prefix = true;
          }
          if (calling_code_size + prefix.size() > best_length &&
              begins_with(phone_number.substr(calling_code_size), prefix)) {
            best_country = &country;
            best_calling_code = &calling_code;
            best_length = calling_code_size + prefix.size();
          }
        }
      }
      if (begins_with(calling_code.calling_code, phone_number)) {
        is_prefix = true;
      }
    }
  }
  if (best_country == nullptr) {
    return promise.set_value(td_api::make_object<td_api::phoneNumberInfo>(
        nullptr, is_prefix ? phone_number_prefix : string(), is_prefix ? string() : phone_number_prefix));
  }

  string formatted_part = phone_number_prefix.substr(best_calling_code->calling_code.size());
  string formatted_phone_number = formatted_part;
  size_t max_matched_digits = 0;
  for (auto &pattern : best_calling_code->patterns) {
    string result;
    size_t current_pattern_pos = 0;
    bool is_failed_match = false;
    size_t matched_digits = 0;
    for (auto &c : formatted_part) {
      while (current_pattern_pos < pattern.size() && pattern[current_pattern_pos] != 'X' &&
             !is_digit(pattern[current_pattern_pos])) {
        result += pattern[current_pattern_pos++];
      }
      if (current_pattern_pos == pattern.size()) {
        result += c;
      } else if (pattern[current_pattern_pos] == 'X') {
        result += c;
        current_pattern_pos++;
      } else {
        CHECK(is_digit(pattern[current_pattern_pos]));
        if (c == pattern[current_pattern_pos]) {
          matched_digits++;
          result += c;
          current_pattern_pos++;
        } else {
          is_failed_match = true;
          break;
        }
      }
    }
    if (!is_failed_match && matched_digits >= max_matched_digits) {
      max_matched_digits = matched_digits;
      formatted_phone_number = std::move(result);
    }
  }

  promise.set_value(td_api::make_object<td_api::phoneNumberInfo>(
      best_country->get_country_info_object(), best_calling_code->calling_code,
      formatted_phone_number.empty() ? formatted_part : formatted_phone_number));
}

void CountryInfoManager::get_current_country_code(Promise<string> &&promise) {
  td_->create_handler<GetNearestDcQuery>(std::move(promise))->send();
}

void CountryInfoManager::load_country_list(string language_code, int32 hash, Promise<Unit> &&promise) {
  auto &queries = pending_load_country_queries_[language_code];
  if (!promise && !queries.empty()) {
    return;
  }
  queries.push_back(std::move(promise));
  if (queries.size() == 1) {
    auto query_promise = PromiseCreator::lambda(
        [actor_id = actor_id(this), language_code](Result<tl_object_ptr<telegram_api::help_CountriesList>> &&result) {
          send_closure(actor_id, &CountryInfoManager::on_get_country_list, language_code, std::move(result));
        });
    td_->create_handler<GetCountriesListQuery>(std::move(query_promise))->send(language_code, hash);
  }
}

void CountryInfoManager::on_get_country_list(const string &language_code,
                                             Result<tl_object_ptr<telegram_api::help_CountriesList>> r_country_list) {
  auto query_it = pending_load_country_queries_.find(language_code);
  CHECK(query_it != pending_load_country_queries_.end());
  auto promises = std::move(query_it->second);
  CHECK(!promises.empty());
  pending_load_country_queries_.erase(query_it);

  auto &countries = countries_[language_code];
  if (r_country_list.is_error()) {
    if (countries != nullptr) {
      countries->next_reload_time = Time::now() + Random::fast(60, 120);
    }
    for (auto &promise : promises) {
      promise.set_error(r_country_list.error().clone());
    }
    return;
  }

  auto country_list = r_country_list.move_as_ok();
  CHECK(country_list != nullptr);
  switch (country_list->get_id()) {
    case telegram_api::help_countriesListNotModified::ID:
      if (countries == nullptr) {
        LOG(ERROR) << "Receive countriesListNotModified for unknown list with language code " << language_code;
        countries_.erase(language_code);
      } else {
        LOG(INFO) << "List of countries with language code " << language_code << " is not modified";
        countries->next_reload_time = Time::now() + Random::fast(86400, 2 * 86400);
      }
      break;
    case telegram_api::help_countriesList::ID: {
      auto list = move_tl_object_as<telegram_api::help_countriesList>(country_list);
      if (countries == nullptr) {
        countries = make_unique<CountryList>();
      }
      for (auto &c : list->countries_) {
        CountryInfo info;
        info.country_code = std::move(c->iso2_);
        info.default_name = std::move(c->default_name_);
        info.name = std::move(c->name_);
        info.is_hidden = std::move(c->hidden_);
        for (auto &code : c->country_codes_) {
          auto r_calling_code = to_integer_safe<int32>(code->country_code_);
          if (r_calling_code.is_error() || r_calling_code.ok() <= 0) {
            LOG(ERROR) << "Receive invalid calling code " << code->country_code_ << " for country "
                       << info.country_code;
          } else {
            CallingCodeInfo calling_code_info;
            calling_code_info.calling_code = std::move(code->country_code_);
            calling_code_info.prefixes = std::move(code->prefixes_);
            if (calling_code_info.prefixes.empty()) {
              calling_code_info.prefixes.resize(1);
            }
            calling_code_info.patterns = std::move(code->patterns_);
            info.calling_codes.push_back(std::move(calling_code_info));
          }
        }
        if (info.calling_codes.empty()) {
          LOG(ERROR) << "Receive empty list of calling codes for " << info.country_code;
          continue;
        }

        countries->countries_.push_back(std::move(info));
      }
      countries->hash = list->hash_;
      countries->next_reload_time = Time::now() + Random::fast(86400, 2 * 86400);
      break;
    }
    default:
      UNREACHABLE();
  }

  for (auto &promise : promises) {
    promise.set_value(Unit());
  }
}

const CountryInfoManager::CountryList *CountryInfoManager::get_country_list(const string &language_code) {
  auto it = countries_.find(language_code);
  if (it == countries_.end()) {
    return nullptr;
  }

  auto *country = it->second.get();
  CHECK(country != nullptr);
  if (country->next_reload_time < Time::now()) {
    load_country_list(language_code, country->hash, Auto());
  }

  return country;
}

}  // namespace td
