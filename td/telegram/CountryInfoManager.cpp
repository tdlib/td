//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/CountryInfoManager.h"

#include "td/telegram/Global.h"
#include "td/telegram/LanguagePackManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/algorithm.h"
#include "td/utils/base64.h"
#include "td/utils/buffer.h"
#include "td/utils/Gzip.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Random.h"
#include "td/utils/Time.h"
#include "td/utils/tl_parsers.h"

namespace td {

class GetNearestDcQuery final : public Td::ResultHandler {
  Promise<string> promise_;

 public:
  explicit GetNearestDcQuery(Promise<string> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create_unauth(telegram_api::help_getNearestDc()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::help_getNearestDc>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    promise_.set_value(std::move(result->country_));
  }

  void on_error(Status status) final {
    if (!G()->is_expected_error(status) && status.message() != "BOT_METHOD_INVALID") {
      LOG(ERROR) << "GetNearestDc returned " << status;
    }
    promise_.set_error(std::move(status));
  }
};

class GetCountriesListQuery final : public Td::ResultHandler {
  Promise<tl_object_ptr<telegram_api::help_CountriesList>> promise_;

 public:
  explicit GetCountriesListQuery(Promise<tl_object_ptr<telegram_api::help_CountriesList>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &language_code, int32 hash) {
    send_query(G()->net_query_creator().create_unauth(telegram_api::help_getCountriesList(language_code, hash)));
  }

  void on_result(BufferSlice packet) final {
    // LOG(ERROR) << base64url_encode(gzencode(packet, 0.9));
    auto result_ptr = fetch_result<telegram_api::help_getCountriesList>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(result_ptr.move_as_ok());
  }

  void on_error(Status status) final {
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
  vector<CountryInfo> countries;
  int32 hash = 0;
  double next_reload_time = 0.0;

  td_api::object_ptr<td_api::countries> get_countries_object() const {
    return td_api::make_object<td_api::countries>(
        transform(countries, [](const CountryInfo &info) { return info.get_country_info_object(); }));
  }
};

CountryInfoManager::CountryInfoManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

CountryInfoManager::~CountryInfoManager() = default;

void CountryInfoManager::start_up() {
  std::lock_guard<std::mutex> country_lock(country_mutex_);
  manager_count_++;
}

void CountryInfoManager::tear_down() {
  parent_.reset();

  std::lock_guard<std::mutex> country_lock(country_mutex_);
  manager_count_--;
  if (manager_count_ == 0 && !countries_.empty()) {
    LOG(INFO) << "Clear country info";
    countries_.clear();
  }
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

  {
    std::lock_guard<std::mutex> country_lock(country_mutex_);
    auto list = get_country_list(this, language_code);
    if (list != nullptr) {
      return promise.set_value(list->get_countries_object());
    }
  }

  if (is_recursive) {
    return promise.set_error(Status::Error(500, "Requested data is inaccessible"));
  }
  load_country_list(language_code, 0,
                    PromiseCreator::lambda([actor_id = actor_id(this), language_code,
                                            promise = std::move(promise)](Result<Unit> &&result) mutable {
                      if (result.is_error()) {
                        return promise.set_error(result.move_as_error());
                      }
                      send_closure(actor_id, &CountryInfoManager::do_get_countries, std::move(language_code), true,
                                   std::move(promise));
                    }));
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
  {
    std::lock_guard<std::mutex> country_lock(country_mutex_);
    auto list = get_country_list(this, language_code);
    if (list != nullptr) {
      return promise.set_value(get_phone_number_info_object(list, phone_number_prefix));
    }
  }

  if (is_recursive) {
    return promise.set_error(Status::Error(500, "Requested data is inaccessible"));
  }
  load_country_list(language_code, 0,
                    PromiseCreator::lambda([actor_id = actor_id(this), phone_number_prefix, language_code,
                                            promise = std::move(promise)](Result<Unit> &&result) mutable {
                      if (result.is_error()) {
                        return promise.set_error(result.move_as_error());
                      }
                      send_closure(actor_id, &CountryInfoManager::do_get_phone_number_info,
                                   std::move(phone_number_prefix), std::move(language_code), true, std::move(promise));
                    }));
}

td_api::object_ptr<td_api::phoneNumberInfo> CountryInfoManager::get_phone_number_info_sync(const string &language_code,
                                                                                           string phone_number_prefix) {
  td::remove_if(phone_number_prefix, [](char c) { return !is_digit(c); });
  if (phone_number_prefix.empty()) {
    return td_api::make_object<td_api::phoneNumberInfo>(nullptr, string(), string());
  }

  std::lock_guard<std::mutex> country_lock(country_mutex_);
  auto list = get_country_list(nullptr, language_code);
  if (list == nullptr) {
    list = get_country_list(nullptr, "en");
  }

  return get_phone_number_info_object(list, phone_number_prefix);
}

td_api::object_ptr<td_api::phoneNumberInfo> CountryInfoManager::get_phone_number_info_object(const CountryList *list,
                                                                                             Slice phone_number) {
  CHECK(list != nullptr);
  const CountryInfo *best_country = nullptr;
  const CallingCodeInfo *best_calling_code = nullptr;
  size_t best_length = 0;
  bool is_prefix = false;  // is phone number a prefix of a valid country_code + prefix
  for (auto &country : list->countries) {
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
    return td_api::make_object<td_api::phoneNumberInfo>(nullptr, is_prefix ? phone_number.str() : string(),
                                                        is_prefix ? string() : phone_number.str());
  }

  Slice formatted_part = phone_number.substr(best_calling_code->calling_code.size());
  string formatted_phone_number;
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
        // result += ' ';
      }
      if (current_pattern_pos >= pattern.size() || pattern[current_pattern_pos] == 'X') {
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
    for (size_t i = current_pattern_pos; i < pattern.size(); i++) {
      if (is_digit(pattern[i])) {
        is_failed_match = true;
      }
    }
    if (!is_failed_match && matched_digits >= max_matched_digits) {
      max_matched_digits = matched_digits;
      while (current_pattern_pos < pattern.size()) {
        if (pattern[current_pattern_pos] == 'X') {
          result.push_back('-');
        } else {
          CHECK(!is_digit(pattern[current_pattern_pos]));
          result.push_back(' ');
        }
        current_pattern_pos++;
      }
      formatted_phone_number = std::move(result);
    }
  }

  return td_api::make_object<td_api::phoneNumberInfo>(
      best_country->get_country_info_object(), best_calling_code->calling_code,
      formatted_phone_number.empty() ? formatted_part.str() : formatted_phone_number);
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

  if (r_country_list.is_error()) {
    {
      std::lock_guard<std::mutex> country_lock(country_mutex_);
      auto it = countries_.find(language_code);
      if (it != countries_.end()) {
        // don't try to reload countries more often than once in 1-2 minutes
        it->second->next_reload_time = max(Time::now() + Random::fast(60, 120), it->second->next_reload_time);

        // if we have data for the language, then we don't need to fail promises
        for (auto &promise : promises) {
          promise.set_value(Unit());
        }
        return;
      }
    }
    for (auto &promise : promises) {
      promise.set_error(r_country_list.error().clone());
    }
    return;
  }

  {
    std::lock_guard<std::mutex> country_lock(country_mutex_);
    on_get_country_list_impl(language_code, r_country_list.move_as_ok());
  }

  for (auto &promise : promises) {
    promise.set_value(Unit());
  }
}

void CountryInfoManager::on_get_country_list_impl(const string &language_code,
                                                  tl_object_ptr<telegram_api::help_CountriesList> country_list) {
  CHECK(country_list != nullptr);
  LOG(DEBUG) << "Receive " << to_string(country_list);
  auto &countries = countries_[language_code];
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
      } else {
        countries->countries.clear();
      }
      for (auto &c : list->countries_) {
        CountryInfo info;
        info.country_code = std::move(c->iso2_);
        info.default_name = std::move(c->default_name_);
        info.name = std::move(c->name_);
        info.is_hidden = c->hidden_;
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

        countries->countries.push_back(std::move(info));
      }
      countries->hash = list->hash_;
      countries->next_reload_time = Time::now() + Random::fast(86400, 2 * 86400);
      break;
    }
    default:
      UNREACHABLE();
  }
}

const CountryInfoManager::CountryList *CountryInfoManager::get_country_list(CountryInfoManager *manager,
                                                                            const string &language_code) {
  auto it = countries_.find(language_code);
  if (it == countries_.end()) {
    if (language_code == "en") {
      static const BufferSlice en = gzdecode(
          base64url_decode(
              "eJyNW0tz48iRrhb1lrpn2h6PfXBMMGIjvN6IHQeJN458iyJBsQlSr1uRrCFrBAIaEJAs_QHf9ifsxQeHD77t_oK27_"
              "4Pvvmyl73sbROkSIBEstAR3S01VVmVlZX55ZdZpf8M__6HX_z1v3_9T0LIv_zHHz7DF7JXqpKjkjv2fJ9GP3sHn_"
              "3P33LlPfiak3Vt9dnx7W1-8WdDtka-Gbg8YON8yafDfG3GfRqwOfwsNZepF1efnS0mWvy9jeeqk7PSD5Mpdfk8oO62_"
              "J4pk4T8eoJYvkE-ltyAT0Ka_02-TP1hOKZpPfaLkmYk9DteKbKxryY5LrmTkDsORfYSzaFkztEGuzpD2A5iV1UV28ICWX_"
              "GUFldWX12Gssm1r0ih6C7B5qnZCVFEduwR05K_oSBGd0t2y1-riqJdWzyoTRjPh9RN2_TmZdeb7-oGdl26sNew3ngp_"
              "e6p8RnfrqS3djrAPSNZKnDkbPe04oJn1lIb9n5hhyU_HCIyOYkUxfrfU9OS6_MH1L-I3URfzcV4RmXS-SbsjeHIwZ3vWD-K5t4Twuzp-"
              "eSDV145uUyOY48no49LPbAXxUt6xzKVXJapu7EoWM2n6Z1MIyCWIcaOSozZ8LDWeocZGk93_ntCki2sKRcJ-fl0H8AE-"
              "TrdO5hdpAkbdOmyz_xHA2wQ-hMqM-37UiWcWfGYy9AXzr1KU9hDWCVvPrs5DY2VizbBNnQD90xT-"
              "uobp1VftNny5fkoAyR7aL7M8U2tiIb-zNAtvQZK0ox84w75GMZ1GY8X6V-OJ9Th84QPbR4__g8V6CH5_"
              "AnBJ9UsxjvYe32CdlP5Luy51Lus3_P29wN8jWIYBrwcA6BYNMhTZ-baibOrUcOyz595U463lV1_dn7FVykzs5enDud0TkWJ1KmDfuw_"
              "jQM0JjXVfH53YB_esH8meJxLmkZcX63iLHo6JC8IM4p5XvQmzn8lSH2LRTX4yolclgB_cab55CDr--KS51_"
              "G8koBTmnGnpONwo5Q1JzkqyB7xdyWkEB_"
              "5Fyum7kJPhekYswv5bTC2bOLMDnhhTFMeQiMycZZk5W5JysqTmlqMFYPacWzZyqGDmtKOc0BeYvqDmjoINsNMaAMfAX1lGLSk7VYaxRz"
              "BkgY-gwD6yjyfB9bMfzRJ7bOMdKlXyseJAo87-tstnv8j32-Lt_w85EkYV2rdTJNxXImJCD8qUflukwmgs9X1mMX5UGOX_"
              "T6U0dTB9JrM8FObOfeQCpyaHuOJ1TkzkRk2-SD5XP_xWw_Phfm08ehCmGU-pmnOXTcVZpRXvxHvLNeaTHdl5a-"
              "J1mSPH4NjmoTLnDkDyuauI8XrHIcYUCGfE8F7f72h9QPK90Fmtvc57F2oaWxJTE6kn5K1jfc7zZMJV3lvrr2_"
              "kP8cceOa14gIT5HvhQeg8QQ-I9DMh-ZUFlkPWTPArB5Mo1rE0fWf6a-"
              "WPsvGVDiIkVwLVK6NPPf6LbefvdG36vPntnJuXuAGteHgHOEM6jZpzZPfjpKxtNo2gLhw4fpedQpELC19N8twqcpQH5lLov6TgxF_"
              "Ptv53ZinxG-z4rauv_nhX1-HtZKxTzStE0kmtckuPqj3zohQFHfVOVhZhQbZGjKnNn1H9I6xjnO9RGVYiLqjfjbuRRGC_"
              "UNZ1k5LvqFfmwmuMN2aLxuY15jIK5Y57EGOkLxii7xsT63Ed11YQh9UJOKspbcZbmmbUKOaqNQqDKPsJdxFhfA3-"
              "pzQMPr8sk4VnUGuSgNnl5DJD4lApJfEhCTEK-B2v7PPAZsu-Yc53EtCEha5MD-5FinHNPVsR1da1PjmvBlHuPKLaB_4rr-"
              "jpw5Tp3t3MRWca4EY-7JPt1_iMaI5puCn2i3iJf16nzEC0iyjdqoRDLWOTU4iPfc9mcI5xTM2NOVL8i7-"
              "vU91g8OXIGa4w82rZhHThr3afuKI2te7KcyC_"
              "5DaddyzdK5KBBhx5aMyibfPtNOJYtkw9vPZoWdydjb5bWQdn0gRWgxXNUASd9FpFCpO7Q5ayYbSxw1vMnSNyYpir0v0YdbA9rA9A3Qk6"
              "xvgTErRKPvwBbTVPj3vKYOL4bTXLS4EOgcgH1sTpcLQhjvNEGeZ-xhbejPmLu9JGGRQ4bdMEgEL5VENu3A7IhdxkqK-"
              "75NLrktAFwyBwvfERyv2qu196JqY1P5GPtp5AGHqCyk39TBfNVcT5uQJxE9kPjZAMjcQ7V6IP9QQ02ow62vlqQNmJlmwc1gEOB_"
              "AzBoP2ipmfW140b8n65-e_LHKrrELWBKp7jLjrLl8iBET-P-fJFi5xcQK2Qb8E_yDqGKt7rRYccg_"
              "wYaBvar1ULitDXLyAfVXwPCnckFxriOviiTw4uKI8IEbKuKV53QI4uQndC_"
              "TRnkzUi9K9mlZw03fEb5CP9wrhPhfYbm4BjTZ9hdRVggxhbmm1y2Jz7lKX7FjlTF9d0TahPQG9UZ7O4id1vWseyV-"
              "S8yhnUlA3qj3bkcEUT-mTzE9lv-vQnTNbUlMy4bPYW8lhttmca2XHdtMHuo112V8S6g681A-q8YNzHFPvLpUWOLumMAvVN44GhZ_"
              "ZUL6_I4aXnj9FekZY4c6SmveySg0v6SDHOZhSTNsP5YqtGDlrMfUFxWBXnhFaDnLZe_MnL6xzvc5maME5aF4t6fOiNcX8zVDE-"
              "tJrkuMV9PqR4zaQZ4lq4BedW8Wae76X7ZJIm5pKtDvm5TaO-"
              "ZIsHQdST7LAnjvQKDS2zVml1yVnH84MpgPQWbydLO8R8tNUjZzYUicjY5ZlLSV9dn3ksf0MOW-"
              "EzoCrma6rYXndQS9MXqIPXBDe9Xxmqzaz93oPf0Ff6ME35zaJ_qC_lo_neafH_l9x37Qib3LddIvttit5rgP20hHw-4UoJ-"
              "TI5arMhdT2kz68VhX7YrsCZLHyhHY7SeQ7qZzXzLrHdJO_bnI2mAXPnAUP7_ook7re3IdfbPs-"
              "3qfuA5gBl2zc2saQNubrNh3jNLIvrt7Yd2W_uBVMPiSVNbD_gY20eTMPoFhTj0_oGn17iSHLfA3LaDn_PAElCf4Lx8WTeTONY-"
              "5octmnwhHFqWS-K174jB2AzHD-LhtBmVokcWYA-oxFis6I411sVcmh5Lh15mM7inphVjdZ1xt4T1p-"
              "QhWdl1aAe9lzwUjbxN9deYJWc6NVakB8sKAYndD6CQgnxi7Vtz5NxmfRp64J8bVF_PqWOI-"
              "wRJziv1SJfLfHUoiM2xvswhvgOzWqTfYs6eC9OEt_7WZBXLKDmM-pv67lnqolxwK2tqI_v4PnP1LfiZoubW1fkAHaI1g-GmqFjl_"
              "xqYSPmu2AmPyqZd9h3VdcUsjDM-hSdtx9wl_8UYvdHphaP7UVjQ58Hi6BHbCzuk1n20g_nzPdpOpeBvtlvL6x-"
              "ZD8o4tEaXnwHYw3IyVJ_HqJ9HlncA7Cu4eypM-ZPO97BaOJa3oI8DvL0mWNxJb7js25Blv2ej9Lxu6dKiTXuFjrSlzl-X6F9QT_Suo_"
              "O6TXqWGz7xBvHFGNkBzCyQ2d8iOUkTfx2olMh7zvsOV-hzhIF0LslPR4PXLjDJwzr6UiSLvSHTp18gHj6wXNWV1nIWnqMUZ0G7Ivv6E_"
              "LSqZdO01y0oGCw6eTEPNftSDmcp02cE4Whf8i5NM1T3HNwbb7je9NPdZpY06oY8AGz_"
              "QF6R3Gd1u4PlDHdNgjdVAc1LPt0QN5iMcQO2M97vt1BmS_w8ETUV-Q43H3kX2e8_"
              "eMovejmiLez5VF9q9maC0LsW0IZbslctgFPJ6h93vi3N6tkf0ui8yA3O9t9gBSPKhbJ1-_9VC7nvPy1v7A7BS_s-g2yNdd-"
              "hjSfGStVUsPOwM1lrkgZ90pd_jjIwxP-54mJ3EFr_W7LXLcpQ-"
              "LR4DYXs3UWyJkjjbY2UvF6tJfjQxbWeSXS87f5ZCCGBSAVoRwzmb9QJZnFt9fdKF-"
              "64bMD7zoAtdbxViiVjB23bXFY0w5s6bs2uSkC7g3h2yM3JVDTG33WDexs9sH-wI_CCdRSGL5sSi2zw05gPW3ucmb_"
              "8T1bPcuOscFir1g66gZff9PJXLwieJ9eDPjDWSvRo57n_8cunzHewBNXDf0rshRz5thbzf3lILYf3o2ObSZj_bxZUNcb_"
              "YG5Ge9cA7R6ebrbMyAAEU72J4nWTfvjIEe8IjeM4QAxsHUbR_Z3INdIuc2Dcd88ax3B4fVtnjUlp_"
              "ZZfKVHb2J8NZNBQQ74hxtV8ipzV5GU-"
              "aAc2M9SiO2XZy0YvkqObDDMdYvi94aiXWtwZk9g8HTtt5TNnrKiGwDanPuTuij52NvV7SMO3r7IrJ1hDcXzGHrd2F7STtJip64Q1qf3e"
              "G2_9hNcmw73hNz8TOTDXG9breW8gC_qLwiibHBbsNeItSk-Tbz3O1c_MaBJLEOFvgBXZYuLloDG5m-b3fIkR0VstRB-J6432FD7NsQ-"
              "w7G39QM3XtgPygdIMOje1cz3hTb9qoHuPBkpHeQoXuffGt__qOX73uzz3-GzNX1P__FHXHsXk-Sk33QFeVL6HJNzmpO3qbOE_"
              "5WoiCuQ-3byBfArS0KlSPDcBhyolQkGfnOvoO4fvHR_pGpyWJ_vCfHtfkzjQpX1Bcz-Fq_Qr7th_5D1AWuUMjqc0F_VMt-"
              "u9Kvkv3KlI7xuNiq7bbqkH6D7Pe9yY530YbQL_sX5Lg_pXx5LY1gVMb7uv4lOe3TH_kbK0ufgynuafVb5KjvPTBgDekcYMacoQ-1S5_"
              "PPP_7NvAbhuSLxFiLnEdnE_2WxA6uCHolfXTp4xvnCzjRB5YwR98SibGyf0UO-h7gPsqJ435Iv0cOIz0ZUjuZX3Cn3e-"
              "Tj32AFD6mY3DDvjekkROk_c_I_r2W_nWkyxN1Qoy7xVy2D9yhT_"
              "kzdtaGoQljrg8x16fuK92Rg6SM330ZlMjR4CF6ls9SsnL8Owjna9mtvuKgQQ4Hk128R8xZBjbJDexSim8Vv4BvDe5Abz-M6C6C-"
              "5vcZbvfN7gnp4PXIdsdX0bmvq8r5Lslj7jm7ojB19_kgynLL9_tROUYhr_Zv59zXSMn15BLX6EEQjB4T_2C-"
              "9vrBvm2HHXU5lPQzp9w0T2T9AU6NcnHgb09FXpnVcicCzDgmrMAcvZ2TOwZid93uh7AOOqGNEAwTI9j56ZOvrqhjsOjpFEPg9BN3_"
              "tpRvy27CZ6m7f49SksJhe1dfTZ3m2DfNdwIPqdvOUNucMgNQfAlXkA370AWs7S8kZindsWOWx5c-8J41TGzly-XPuafNt0A-"
              "a7i4IEVOiw4NmDxJjmqoYhJb6P-y53NXJwx2YM4-empu_ClMX6d1DP332fqucXP1Ok-E5RWdZG6zXvozpmwaeWL_"
              "MRPNqTdCLEhHuLHN7vepcV93Jx2RtyfM9nQzp8xrmwhr4Z-cf__p_7_0VkiUI")
              .ok());
      TlBufferParser parser(&en);
      auto result = telegram_api::help_getCountriesList::fetch_result(parser);
      parser.fetch_end();
      CHECK(parser.get_error() == nullptr);
      on_get_country_list_impl(language_code, std::move(result));

      it = countries_.find(language_code);
      CHECK(it != countries_.end())
      auto *country = it->second.get();
      if (manager != nullptr) {
        manager->load_country_list(language_code, country->hash, Auto());
      }
      return country;
    }
    return nullptr;
  }

  auto *country = it->second.get();
  CHECK(country != nullptr);
  if (manager != nullptr && country->next_reload_time < Time::now()) {
    manager->load_country_list(language_code, country->hash, Auto());
  }

  return country;
}

int32 CountryInfoManager::manager_count_ = 0;
std::mutex CountryInfoManager::country_mutex_;
std::unordered_map<string, unique_ptr<CountryInfoManager::CountryList>> CountryInfoManager::countries_;

}  // namespace td
