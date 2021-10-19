//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
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

  void on_result(uint64 id, BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::help_getNearestDc>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    promise_.set_value(std::move(result->country_));
  }

  void on_error(uint64 id, Status status) final {
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

  void on_result(uint64 id, BufferSlice packet) final {
    // LOG(ERROR) << base64url_encode(gzencode(packet, 0.9));
    auto result_ptr = fetch_result<telegram_api::help_getCountriesList>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    promise_.set_value(result_ptr.move_as_ok());
  }

  void on_error(uint64 id, Status status) final {
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
              "eJyNW81uI0lyzhb13-qeaXs8uwdjQMDAehfwLMj6ryP_RZFFsVmk_m5JMofMUbFKU6ySVnqBve0jGAb2sNiDb_YTtH034LfYi1_"
              "BUaTEKlYFsxrobqmpjMzIyIgvvohM_Wv4P3_8h__6z3_8GyHkn_70xy_whexV6uSo4k4936fRz97BZ__"
              "334XqHnwtyLr29tnxzU1x9WdLtkG-G7k8YNNixafjYmPBfRqwJfwsM5epl98-e7-aaPX3Jp6rSd5XfprNqcuXAXXT8numTBLymwli-"
              "Rb5VHEDPgtp8TfFKvXH4ZRm9dgvS5qR0O_"
              "4TZGtfbXJccWdhdxxKLKXaA4ld44u2NUZw3YQu6qq2BYWyPoLhsrqyttnp7FsYt1Lcgi6e6B5RlZSFLENB-"
              "Sk4s8YmNFN2W71c1VJrGOTj5UF8_mEukWbLrzsevtlzci30xD2Gi4DP7vXPSU-"
              "89O15M32XkegbyRLHY6c9Z5WTvjMSj5l52tyUPHDMSJbkExdrPcdOa28MH9M-c_URfzdVIRnXK2Q76reEo4Y3PWc-"
              "S9s5j2uzJ6dSzZ04ZlXq-Q48ng69bDYA39VtLxzqNbJaZW6M4dO2XKe1cEwSmIdGuSoypwZDxeZc5ClzXxnN29AksKSapOcVUP_"
              "HkxQbNKlh9lBkrRtm67_xHO0wA6hM6M-T9uRrOPOjMeeg7507lOewRrAKvnts5Ob2FixbBtkQz90pzyro5o6q-"
              "K2z1YvyEEVIttF92eKbWxFNvYXgGzZM1aUcu4Z98inKqjNeLFO_XC5pA5dIHpo8f7xeS5BD8_hjwg-qWZ5O2bT-_9Mfqh6LuU--"
              "5eizd2g2IAIpgEPlxAINh3T7LmpZuLcBuSw6tMX7mTjXVU3n314XTh7dvbq3OmCLrE4kXJtOIT152GAxryuis_"
              "vGvzTC5ZPFI9zScuJ89tVjEVHh-QFcU6p3oHezOEvDLFvqbwZV6uQwxroN90-hwJ8fVde6_"
              "zbSEYpyQXV0Au6USoYklqQZA18v1TQSgr4j1TQdaMgwfeKXIb5tYJeMgtmCT43pCiOIReZBckwC7IiF2RNLShlDcbqBbVsFlTFKGhlua"
              "ApMH9JLRglHWSjMQaMgb-wjlpWCqoOY41ywQAZQ4d5YB1Nhu9jO54l8tzWOdbq5FPNg0RZ_G2dLX5fHLCH3_"
              "8OOxNFFtq11iTf1SBjQg4qVn5ap8NoLvR8ZTF-1Vrk7FWnV3UwfSSxPufkvf3EA0hNDnWn2ZyazImYfJt8rH35j4AVp__"
              "cfvQgTDGcUrfjrJiNs1on2ot3X2wvIz3SeWnld5ohxeO75KA25w5D8riqifN4zSLHNQpkxPNc3O4bf0DxvNZbrZ3mPKu1DS2JKYnVk_"
              "KXsL7neItxJu-s9dfT-Q_xxwE5rXmAhMUB-FB2DxBD4j2MyH5tRWWQ9dM8KoXJtStYmz6w4hXzp9h5y4YQE2uAa7XQp1_-QtN5-"
              "90rfr999s5Myt0C1jw_AJwhnEfNObM78NMXNplH0RaOHT7JzqFIpYSvZ_luHThLC_IpdZ-zcWKu5tt_PbMN_YzmKmub_74v6_"
              "H3slYqF5WyaSTXuCDH9Z_52AsDjvqmKgsxod4hR3XmLqh_"
              "n9UxzneojeoQF3Vvwd3IozBeqGs6ycl39Uvy8W2OV2SLxhe25jFK5o55EmOkrxij7BoT63MX1VUzhtQLBaksp-"
              "IsyzMbNXLUmIRAlX2Eu4ixvgH-0lgGHl6XScKzaLTIQWP2_BAg8SmVkviQhJiE_ADW9nngM2TfMec6iWlDQtYmB_YDxTjnnqyI6-"
              "rGkBw3gjn3HlBsA_8V1_VN4MpN7qZzEVnHuBGPuyD7Tf4zGiOabgp9otkh3zapcx8tIso3aqkUy1jk1OIT33PZkiOcUzNjTtS8JB-"
              "a1PdYPDlyBhuMPErbsAmctelTd5LF1j1ZTuSX4pbTbuRbFXLQomMPrRmUbb79KhzLVsnH1x5Nh7uzqbfI6qBs-"
              "8AboMVz1AEnfRaRQqTu0OW8mG2tcNbzZ0jcmKYq9L9WE2wPawPQt0JOsb4ExK0Sjz8HW80z417zmDi-"
              "W21y0uJjoHIB9bE6XC0JY7zVBXmfsZW3oz5i7vSRlkUOW3TFIBC-VRLbtweyIXcZKivu-bT65LQFcMgcL3xAcr9qbtbeiamtz-"
              "RT45eQBh6gslN8VQXzVXE-bkGcRPZD42QLI3EO1RqC_UENtqAOtr5akrZiJc2DWsChQH6BYNB-WdNz6-"
              "vWNfmw3vyPVQ7VdYjaQBXPcRud5XPkwIifx3z5vENOzqFWKHbgH2QdQxXv9bxHjkF-CrQN7deqJUXo6-eQj2q-"
              "B4U7kgsNcR18PiQH55RHhAhZ1xSvOyJH56E7o36Ws8kaEfpXu05O2u70FfKRfmHcpzqNYTBeuw041vYZVlcBNoixpd0lh-"
              "2lT1m2b1EwdXFN14b6BPRGdTbL29j9qnUse0nO6pxBTdmi_mRHDlc0oU-2P5P9tk9_wWRNTcmNy_ZgJY_VZnumkR_XbRvsPtlld0WsO_"
              "haO6DOM8Z9TLG_XFjk6IIuKFDfLB4Yem5P9eKSHF54_hTtFWmJM0dq2os-"
              "ObigDxTjbEY5aTOcL3Ya5KDD3GcUh1VxTui0yGnn2Z89vyzxPpepCeOkc76qx8feFPc3QxXjQ6dNjjvc52OK10yaIa6FO3BuNW_h-"
              "V62TyZpYi7Z6ZG_t2nUl-zwIIh6kj32yJFeoaHl1iqdPnnf8_xgDiCd4u1kbYeYj3YG5L0NRSIydn3mUtJXN2cey1-Tw074BKiK-"
              "Zoqttct1NL0GergDcHN7leGajNvv3fgN_SF3s8zfrPqH-pr-Wi-d1r8_zX33TjCNvftVsh-l6L3GmA_"
              "LSFfTLhSQr5KjrpsTF0P6fNrZaEfdmtwJitf6IaTbJ6D-lnNvUvstsmHLmeTecDcZcDQvr8iifvtXcj1ts-"
              "LXereozlASfvGNpZ0IVd3-RivmWVx_da1I_stvWDuIbGkie0HfKzLg3kY3YJifFrf4tNrHEnue0ROu-"
              "EfGCBJ6M8wPp7Mm1kc616Rwy4NHjFOLetl8dq35ABshuNn2RDazKqQIwvQZzJBbFYW53qrRg4tz6UTD9NZ3BOz6tG6ztR7xPoTsvCsrA"
              "bUw54LXspm_vbaK6ySE71aC_KDBcXgjC4nUCghfrGx7VkyLpM-bZ2Tby3qL-fUcYQ94gTntTrkmzWeWnTCpngfxhDfoVldsm9RB-_"
              "FSeJ7PwvyigXUfEH9tJ57ppoYB9zaivr4Dp7_TD0VNylubl2SA9ghWj8Yao6OffLrlY2Y74KZ_Khk3mHft7qmlIdh1ufovP2Au_"
              "yXELs_MrV47CAaG_o8WAU9YmNxn8yy1364ZL5Ps7kM9M1_e2ENI_tBEY_W8OI7GGtETtb68xDt88jiHoB1BWdPnSl_"
              "3PEORhPX8hbkcZCnTxyLK_Edn3UDsuwPfJKN3z1VSqxxu9KRPi_x-wrtK_"
              "qR1l10Ti9RxyLtE68cU4yRPcDIHl3wMZaTNPHbiV6NfOixp2KNOmsUQO-"
              "W9Hg8cOEenzGspyNJutAfek3yEeLpJ895u8pC1tJjjOq1YF98R39aVnLt2muTkx4UHD6dhZj_qiUxl-t1gXOyKPxXIZ-"
              "tecobDpbuN34w9VinrTmhjgEbPNFnpHcY323h-kAd02MP1EFxUM-3xwDkIR5D7Iz1uO_"
              "XG5H9HgdPRH1BjsfdRfZ5Kt4xit6Paop4P5cW2b9coLUsxLYhlO1XyGEf8HiB3u-Jc3u_Qfb7LDIDcr-33QPI8KB-k3z72kPte87za_"
              "sDs1P8zqLfIt_26UNIi5G13lp62Bmoscw5ed-fc4c_PMDwrO9pchJX8Fq_3yHHfXq_egSI7dXMvCVC5uiCnb1MrK791cixlUV-"
              "teb8fQ4piEEBaEUI52zXD2R9ZvH9RR_qt37I_MCLLnC9txhL1ArGrru2eIwp59aUfZuc9AH3lpCNkbtyiKl0j3UbO_tDsC_"
              "wg3AWhSSWH8ti-1yTA1g_zU1e_SeuZ_u30TmuUOwZW0fN6ft_rpCDzxTvw5s5byAHDXI8-PLX0OU73gNo4rphcEmOBt4Ce7u5p5TE_"
              "jOwyaHNfLSPLxviWmswIn83CJcQnW6xyaYMCFC0g_Q8ybp5ZwwMgEcMniAEMA6mpn1kew92hZzZNJzy1bPeHRxW04R7savkGzt6E-"
              "FtmgoIdsQ52q6RU5s9T-bMAefGepRGfOZx0orl6-"
              "TADqdYvyx6ayTWtQFn9gQGz9p6T9nqKSOyLajNuTujD56PvV3Rcu7o7fPI1hHenDOHbd6F7SXtJCl64g5pc3aHab-32-"
              "TYdrxH5uJnJhviet3urOUBflF5RRJjg92FvUSoSYtd5rnpXPzKgSSxDhb4AV2XLi5aAxu5vm_3yJEdFbLUQfieOAZtiH0bYt_B-Juao_"
              "sA7AelA2R4dO9qzpti237rAa48Gekd5Og-JN_bX_7sFYfe4stfIXP1_S__7k44dq8nyck-6BvlS-"
              "hyRd43nKJNnUf8rURJXIfaN5EvgFtbFCpHhuEw5ESpTHLynX0Lcf3so_0jU5PF_nhHjhvLJxoVrqgv5vC1YY18Pwz9-6gLXKOQ1ZeC_"
              "qiW_3ZlWCf7tTmd4nGRqu1SdciwRfaH3mzHu2hD6JfDc3I8nFO-vpZGMCrnfd3wgpwO6c_"
              "8lZVlz8EU97SGHXI09O4ZsIZsDjBjzjCE2mXIF57_Yxf4DUPyRWKsRc6is4l-S2IHVwS9kj669vGt8wWcGAJLWKJvicRYObwkB0MPcB_"
              "lxHE_ZDggh5GeDKmdzK-40x4OyachQAqf0im44dAb08gJsv5n5P9ey_Aq0uWROiHG3WIuOwTuMKT8CTtrw9CEMTeEmBtS94XuyEFSzu-"
              "-jCrkaHQfPctnGVk5_h2Es41sqq84apHD0WwX7xFzlpFNCiO7kuFb5a_gW6Nb0NsPI7qL4P42d0n3-"
              "0Z35HT0Mma748vI3fdVjfyw5hFX3J0w-PqbYjBnxfW7nagcw_A3__"
              "dzrhrk5Apy6QuUQAgG76lfcX971SLfV6OO2nIO2vkzLrpnkr5Cpzb5NLLTU6F3VqXcuQADrjgLIGenY2LPSPy-"
              "09UIxlE3pAGCYXocO9dN8s01dRweJY1mGIRu9t5PM-K3ZdfR27zVr09hMbmqraPP9m5a5IeWA9HvFC1vzB0GqTkArswD-"
              "O4Z0HKRlTcS69x0yGHHW3qPGKcyduby9dpX5Pu2GzDfXRUkoEKPBU8eJMYsVzUMKfF93He5bZCDW7ZgGD83NX0XpqzWv4V6_vbHTD2_-"
              "pkixXeKyro22qx5F9UxKz61fpmP4NGepBMhJtxZ5PBu17usuJeLy16T4zu-GNPxE86FNfTNyJ__99_-9v8o_old")
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
