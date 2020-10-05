//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ConfigShared.h"

#include "td/telegram/td_api.h"

#include "td/utils/logging.h"
#include "td/utils/misc.h"

namespace td {

ConfigShared::ConfigShared(std::shared_ptr<KeyValueSyncInterface> config_pmc) : config_pmc_(std::move(config_pmc)) {
}

void ConfigShared::set_callback(unique_ptr<Callback> callback) {
  callback_ = std::move(callback);
  if (callback_ == nullptr) {
    return;
  }

  for (auto key_value : config_pmc_->get_all()) {
    on_option_updated(key_value.first);
  }
}

void ConfigShared::set_option_boolean(Slice name, bool value) {
  if (set_option(name, value ? Slice("Btrue") : Slice("Bfalse"))) {
    on_option_updated(name);
  }
}

void ConfigShared::set_option_empty(Slice name) {
  if (set_option(name, Slice())) {
    on_option_updated(name);
  }
}

void ConfigShared::set_option_integer(Slice name, int64 value) {
  if (set_option(name, PSLICE() << "I" << value)) {
    on_option_updated(name);
  }
}

void ConfigShared::set_option_string(Slice name, Slice value) {
  if (set_option(name, PSLICE() << "S" << value)) {
    on_option_updated(name);
  }
}

bool ConfigShared::have_option(Slice name) const {
  return config_pmc_->isset(name.str());
}

string ConfigShared::get_option(Slice name) const {
  return config_pmc_->get(name.str());
}

std::unordered_map<string, string> ConfigShared::get_options() const {
  return config_pmc_->get_all();
}

bool ConfigShared::get_option_boolean(Slice name, bool default_value) const {
  auto value = get_option(name);
  if (value.empty()) {
    return default_value;
  }
  if (value == "Btrue") {
    return true;
  }
  if (value == "Bfalse") {
    return false;
  }
  LOG(ERROR) << "Found \"" << value << "\" instead of boolean option";
  return default_value;
}

int64 ConfigShared::get_option_integer(Slice name, int64 default_value) const {
  auto str_value = get_option(name);
  if (str_value.empty()) {
    return default_value;
  }
  if (str_value[0] != 'I') {
    LOG(ERROR) << "Found \"" << str_value << "\" instead of integer option";
    return default_value;
  }
  return to_integer<int64>(str_value.substr(1));
}

string ConfigShared::get_option_string(Slice name, string default_value) const {
  auto str_value = get_option(name);
  if (str_value.empty()) {
    return default_value;
  }
  if (str_value[0] != 'S') {
    LOG(ERROR) << "Found \"" << str_value << "\" instead of string option";
    return default_value;
  }
  return str_value.substr(1);
}

tl_object_ptr<td_api::OptionValue> ConfigShared::get_option_value(Slice name) const {
  return get_option_value_object(get_option(name));
}

bool ConfigShared::set_option(Slice name, Slice value) {
  if (value.empty()) {
    return config_pmc_->erase(name.str()) != 0;
  } else {
    return config_pmc_->set(name.str(), value.str()) != 0;
  }
}

tl_object_ptr<td_api::OptionValue> ConfigShared::get_option_value_object(Slice value) {
  if (value.empty()) {
    return make_tl_object<td_api::optionValueEmpty>();
  }

  switch (value[0]) {
    case 'B':
      if (value == "Btrue") {
        return make_tl_object<td_api::optionValueBoolean>(true);
      }
      if (value == "Bfalse") {
        return make_tl_object<td_api::optionValueBoolean>(false);
      }
      break;
    case 'I':
      return make_tl_object<td_api::optionValueInteger>(to_integer<int64>(value.substr(1)));
    case 'S':
      return make_tl_object<td_api::optionValueString>(value.substr(1).str());
  }

  return make_tl_object<td_api::optionValueString>(value.str());
}

void ConfigShared::on_option_updated(Slice name) const {
  if (callback_ != nullptr) {
    callback_->on_option_updated(name.str(), get_option(name));
  }
}

}  // namespace td
