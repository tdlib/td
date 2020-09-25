//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

#include <unordered_map>

namespace td {

class Td;

class CountryInfoManager : public Actor {
 public:
  CountryInfoManager(Td *td, ActorShared<> parent);

  void get_countries(Promise<td_api::object_ptr<td_api::countries>> &&promise);

  void get_current_country_code(Promise<string> &&promise);

  void get_phone_number_info(string phone_number_prefix,
                             Promise<td_api::object_ptr<td_api::phoneNumberInfo>> &&promise);

  CountryInfoManager(const CountryInfoManager &) = delete;
  CountryInfoManager &operator=(const CountryInfoManager &) = delete;
  CountryInfoManager(CountryInfoManager &&) = delete;
  CountryInfoManager &operator=(CountryInfoManager &&) = delete;
  ~CountryInfoManager() override;

 private:
  void tear_down() override;

  struct CallingCodeInfo;
  struct CountryInfo;
  struct CountryList;

  string get_main_language_code();

  void do_get_countries(string language_code, bool is_recursive,
                        Promise<td_api::object_ptr<td_api::countries>> &&promise);

  void do_get_phone_number_info(string phone_number_prefix, string language_code, bool is_recursive,
                                Promise<td_api::object_ptr<td_api::phoneNumberInfo>> &&promise);

  void load_country_list(string language_code, int32 hash, Promise<Unit> &&promise);

  void on_get_country_list(const string &language_code,
                           Result<tl_object_ptr<telegram_api::help_CountriesList>> r_country_list);

  void on_get_country_list_impl(const string &language_code,
                                tl_object_ptr<telegram_api::help_CountriesList> country_list);

  const CountryList *get_country_list(const string &language_code);

  std::unordered_map<string, vector<Promise<Unit>>> pending_load_country_queries_;
  std::unordered_map<string, unique_ptr<CountryList>> countries_;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
