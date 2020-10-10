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

#include "td/utils/base64.h"
#include "td/utils/buffer.h"
#include "td/utils/Gzip.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"
#include "td/utils/tl_parsers.h"

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
    hash = 0;
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

  if (r_country_list.is_error()) {
    auto it = countries_.find(language_code);
    if (it != countries_.end()) {
      it->second->next_reload_time = Time::now() + Random::fast(60, 120);
    }
    for (auto &promise : promises) {
      promise.set_error(r_country_list.error().clone());
    }
    return;
  }

  on_get_country_list_impl(language_code, r_country_list.move_as_ok());

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
}

const CountryInfoManager::CountryList *CountryInfoManager::get_country_list(const string &language_code) {
  auto it = countries_.find(language_code);
  if (it == countries_.end()) {
    if (language_code == "en") {
      static const BufferSlice en = gzdecode(
          base64url_decode(
              "eJyNW81y6kiWzmv8f3-qbnd19SwmKhQxET3dEV0doH8tAQPGIMxF4Gt7l0AWZFlILiHZbb9A7-YRZjOLjl7MbuYJ7sx-"
              "IuYdZjGbeYU5AhsJ6ZC6EbbBIk_myfP7nZPJP0f_9Zff_Me___3_EkL-4Z_-8gVeyF71jBxVvakfBDT-7A08-7__LNX24LWkGPrrs-"
              "Pra2n1s0XbIN-NPB6yqVQN6FhqLHhAQ7aEz3JzWUbl9dnb1USr3-tkriZ5W_1pNqceX4bUy9LvWQpJ0W8mSOhb5GPVC_"
              "ksotLvpBoNxtGU5vnYr8i6meLv-"
              "JWRrX21yXHVm0XcdSmyl3gOtXCOLsjVHcN2ELlqmlgWNtAGC4bSGurrs9OENrXuJTkE3n3gPEcrq6pYhgNyUg1mDMToZWS3-lxTU-"
              "s45EN1wQI-oZ7k0IWfX2-_opvFchrCXqNlGOT3uqcmOj9dU15v73UE_"
              "Ma01OWIrvf0SspmVvQZOX8mB9UgGiO0JdkyxHzfktPqMwvGlP9MPcTeLVWo41qVfFfzl6BiMNdzFjyzmf-wEnt-"
              "LsU0hDqv1chxbPF06mO-B_"
              "aq6kV6qJ2R0xr1Zi6dsuU8z4NplsU8NMhRjbkzHi1yelDkzXzvrl8DSSaW1JrkXS0K7kAEUpMufUwOsqxvy3T9k8zRAjlE7owGPCtHsv"
              "Y7Kxl7DvzSeUB5LtZArFJen51cJ8JKaNtAGwWRN-"
              "V5HrWMrqRtm61dkIMaeLaH7s8Sy9iOZRwsILLldayqlUId98jHGrDNuHRGg2i5pC5dIHzoyf7xeS6BD9_lD0h80qzKts9m9_-J_"
              "FDzPcoD9kfJ4V4oNcCDacijJTiCQ8c0rzfNSultQA5rAX3mbt7fNW3z7P3LwnndOSu90wVdYn4iF8pwCOvPoxD1eUMT6-"
              "8z2KcfLh8p7ueyXuDnNysfi1WH5AVxTqndAt_M5c8MkW-5shlXr5LDOvA33dZDCV7fVNY8_z6mUctKSTONkmGWS6aslWRFB9svl_"
              "SyCvYjlwzDLMnwXlUqML9eMspWySrDc1OO_"
              "RhykVWSTaukqEpJ0bWSWtFhrFHSKlZJU82SXlFKugrzl7WSWTaANh5jwhj4hXW0ilrSDBhrVkom0JgGzAPr6Aq8T-"
              "T4LpXntvRYPyMf6z4kSun3Z2zxJ2nA7v_0B0wnqiKUa71JvqtDxoQcJFV_WqfDeC5Uv4o4ftVb5N0LTy_"
              "sYPzIYn7OyVvnkYeQmlzqTfM5NZ0TMfo2-VD_8m8hk6b_2H7wwU2xOCW283on3od_J7WXMQ_ZnLSyOd2Uk_"
              "FdclCfc5chOVzTxTm8bpPjOgUg4vseLvONLaCxvN5brZ3FO6u1TT0dT1Krp-kvYX3f9RfjXM5Z829kcx9iiwNyWvchCkoDsJ_8HsB_"
              "xHsYkf36CsYg62cxVCYe169gbXrPpCsWTDFdK6YwHtYhptWjgH75K83m7Dcvsfv12RsrTXcDcebpHkIZgne0Ap3dgo0-"
              "s8k89rRo7PJJfg5VLqfsPI91zwCvtCCXUu8p7yPWar79tc42Qov3_aGiSanpyPuKIemb_"
              "2EtRS9XJLVimem1Lsjx2c987EchR21UU4Rx4axDjs6Yt6DBXZ7XJOehsjoD_"
              "zjzF9yLLQvDhoZukIKcd3ZJPrzO8RLd4vGlrXnMsrVjntQY-SvGqLvGJPzcxrXVjCE1Q0muKBl_y2PNRp0cNSYRwOUAwS_"
              "ieN8Au2ksQx-vzWShLhotctCYPd2HiJ_K5XScSIeaFP0A1g54GDBk3wnuOknMM0XrkAPnnmK4c09RxbV1Y0iOG-Gc-_"
              "dojAP7Fdf2TcDLTe5l8xFZ-7qZjLsg-03-c9ZHVuN0I8GAzQ75tkndu3hCUY7RyuWExianNp8EvseWHMGYupVgoOYled-kgc-SyRF5b-"
              "LiUVZeTcCozYB6k3w83VOUVE6Rtgx0Q9-qkoMWHftojaBu4-sX4oS2Rj689GQ63JtN_UWeB3Vb36-"
              "ldTLHGcTGgMUgEKkzDKXIP1ur2OoHM8RHLEsT2lqrCbKHtSG4tyJOsT4E-KiajD8HWc1z415yl9iXW21y0uJjgG4hDbC6WysL_"
              "bnVBfqAsZVlozZi7bSRlk0OW3SFGhB8VRbLtwe0EfcYSivu8bT65LQFoY-5fnSP5HvN2qy9M362PpGPjV8iGvoQgV3phRXMVsU5uAV-"
              "EssP9ZOteIjjptYQ5A9ssAV1sfW1srzlK1ns0wLcBPQLJKbtV3SjsJ5ufSbv15v_"
              "scahmo5QGWjiOW5iXT7FBozYeYKRzzvk5BxqA6kDf5B1TE281_MeOQb6KUA1tD-rlVWhrZ9D7qkHPhTqSN4zxfXA-"
              "ZAcnFMegx9kXUu87ogcnUfejAZ5nKboRGhf7TNy0vamLyEf6Q8mfanTJAwma7chjrUDhtVREBvEsaXdJYftZUBZvk9RsgxxDdeGmgT4R"
              "nm2Ktux-4XrhPaSvDvjDGrIFg0mO_K1qgttsv2J7LcD-gtGa-lqoV-"
              "2Byt6rB7bs8xiv247IPfJLrmrYt7B1tohdZ8wnGOJ7eXCJkcXdEEB5ubjgWkU9lAvLsnhhR9M0d6QntI5Usde9MnBBb2nGD4zK2mZ4di"
              "w0yAHHeY9oXFYE-eEToucdp6C2dPzEu9rWbrQTzrnqxp87E9xezM1cXzotMlxhwd8TPH6SDfF9W8H9Fb3F37g5_"
              "tism4JddbpkV87NO5DdngYxj3IHnvgSG_"
              "Q1Avrkk6fvO35QTiHIJ3B6GQthwSPdgbkrQMFITJ2rXM5basbnSf0n8lhJ3qEqIrZmiaW1w3Uz_QJat8NwM3vV4HKsmi_"
              "t2A39JnezXN2s-oXGmv6eL43evL_GvtuDGEb-3arZL9L0XMMkJ-eopdSppSir5GjLhtTz0f6-"
              "npFaIfdOuhkZQvdaJLPc1Ara4Vnh902ed_lbDIPmbcMWbbeWtmBKivJeMjrTsClLvXu0HivZu1gO250IS93-RivhRVxXdZ1Ylkt_"
              "XDuI36ji2UF2KvLw3kUn3Bi2NnYws6vDZSEfkROu9GfGUSNKJhh2DudI_"
              "Mxq3tFDrs0fMDws2JUxGvfkAOQGR4rK6ZQZnaVHNkQaSYTRGYVcV636-TQ9j068TGexT0v-"
              "yxe1536D1jfQRHqym5A7et7YJFsFvh5e1RSvVgbcoENhd-"
              "MLidQFCF2sZHtu7QPpn3APiff2jRYzqnrCnvAKXxrd8g369hp0wmb4v0VU3w-ZnfJvk1dvMcmi8_"
              "0bMghNsDwBQ2yfO5ZWmoc4Gg77tG7eK6zjIzfZHC4fUkOYIdorWBqBTz2yd-tZMQCD8QUxOXxDvm-"
              "1jDlonhlf4r1HYTc479E2NmQpSdjB_HYKODhyukRGYv7X7aztsMlCwKaz1vAb_G9CnsYyw8KdrReF5-v2CNysuafR2hPRxHX-_"
              "YV6J66U_6w446LLq7bbcjZQE8fOeZX4vM7-xpo2Z_5JO-_"
              "e5qcWuNmxSN9WuLnEfpX9Bnt21hPz3F3ImsTL3hSHCN7ECN7dMHHWE7SxfcienXyvscepTp111EAPTsykvGAe3t8xrD-"
              "jSwbQnvoNckH8KeffPf1qArrOyYxqteCffEdfWdFLZRrr01OelBcBHQWYfarlcW4rdcFfMli91-5fL6-qWzwVra3-"
              "N4yEp625oSaBWTwSJ-QPmFydoXzAzVLj91TF42DRrE8BkAP_hhhOjaSHl9vRPZ7HCwRtYUES_VuY_"
              "k8SreMomefuirez6VN9i8XaN0Kvm0KaftVctiHeLxAz-_Eub3fIPt9FosBOb_brvdzOKjfJN--9Ev7vvv00urA5JT0z_"
              "st8m2f3kdUiqX12r7DdKAlNOfkbX_OXX5_D8Pztqcr6biC1_"
              "X9Djnu07vVBT9sr1bunhAyRxfk7Od8dW2vZoGsbPLbNb7vc0hBDIo9O45w7natQNY6S84l-lCr9SMWhH58QOu_-"
              "liqLjB3naElYyylsH7sO-SkD3FvCdkYOQsHn8r2U7djZ38I8gV8EM1il8TyY0Usn8_kANbPYpMX-0lq1_"
              "5NrMdVFHvC1tEKevyfquTgE8V77lbB_"
              "cZBgxwPvvwt8viO835dXDcMLsnRwF9g9zL31LLYfgYOOXRYgPbsFVNcaw1G5FeDaAne6UlNNmUAgOIdZOdJ18g7fWAAOGLwCC6AYTAta"
              "yPbe3Cq5J1DoylfXdndgWF1XbgXp0a-ceI7D_6mgYDEjiRHO3Vy6rCnyZy5YNxYP9JMdJ4krYT-"
              "jBw40RTrjcX3iMS8NkBnjyDwvKz31K3-MULbgtqcezN67wfY3RS94OzdOY9lHcebc-"
              "ayzZ2vvbScZNVInRdtdHeYtXunTY4d139gHq4zxRTX605nTQ_hF6VXZXFscLqwlzhqUqnLfC-"
              "bi18wkCzmwQY7oOvSxUNrYLPQ9p0eOXLiQpa6CN4T-6ADvu-A77sYftMKeB-A_KB0gAyP7l0ruC_sOK_9vpUlI72DAt6H5Hvny7_"
              "40tBffPkbZK5-8OVfvQnHzvBkJd3zfIV8KV6uyNuGKznUfcDvQJTFdahzHdsCmLVNoXJkWByGnChXSEG-c27Ar58CtH9k6YrYHm_"
              "JcWP5SOPCFbXFArw2rJPvh1FwF3d86xSy-lLQC9WL76QMz8h-"
              "fU6nuF9kartMHTJskf2hP9tx59kU2uXwnBwP55Svj6CRGFVwf254QU6H9Gf-gsryerDEPa1hhxwN_TsGqCGfA6wEMwyhdhnyhR_"
              "82AV8w5B8kRprk3exbuJvQOzAisBX2kbXNr6lX4gTQ0AJS_SOkDhWDi_JwdCHuI9i4qQfMhyQw5hPhtRO1lecXw-"
              "H5OMQQgqf0imY4dAf09gI8vZnFn9nZXgV8_JA3QjDbgmWHQJ2GFL-iOnaNHWhzw3B54bUe6Y7cpBc8L2WUZUcje7iK_"
              "csR6sk3y9I7txl-oqjFjkczXbhHjFmGTmkNHKqObxV-Qq8NboBvoMohrtI3N_GLtl-3-iWnI6ex2y3f2X9e5v-qk5-WGOIK-5NGLz-"
              "TgrnTFrfz4lLMSz2Fn_v5qpBTq4gjz5D-YPE3z3tK85pr1rk-1rcTVvOgbtgxkXnSfJX8NQmH0dOdir0bKpcOBf4_xVnIeTrrD_"
              "smanvMV2NYBz1Ihoi8ctI_OZzk3zzmboujxNGMwojL3--p5vJHbLP8X271deiMH9c1dXxs73rFvmh5YLnu5Ltj7nLIC2HgJN5CO-"
              "eIFIu8vRmap3rDjns-Ev_AcNT5s48vl77inzf9kIWeKtiBFjosfDRh6SYx6mmKafeJz2XmwY5uGELhmFzSzd2xZPV-jdQy9_8mKvlV5-"
              "pcnJ2qK7ros2at3ENs8JS6xv3SCzakw0ijAe3Njm83XX_Kunj4rSfyfEtX4zp-BHHwTp6N2T6P3_97_8HHeuAFA==")
              .ok());
      TlBufferParser parser(&en);
      auto result = telegram_api::help_getCountriesList::fetch_result(parser);
      parser.fetch_end();
      CHECK(parser.get_error() == nullptr);
      on_get_country_list_impl(language_code, std::move(result));

      it = countries_.find(language_code);
      CHECK(it != countries_.end())
      auto *country = it->second.get();
      load_country_list(language_code, country->hash, Auto());
      return country;
    }
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
