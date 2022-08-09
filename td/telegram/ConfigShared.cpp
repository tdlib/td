//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ConfigShared.h"

#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/SliceBuilder.h"

namespace td {

ConfigShared::ConfigShared(std::shared_ptr<KeyValueSyncInterface> config_pmc) : config_pmc_(std::move(config_pmc)) {
}

void ConfigShared::set_callback(unique_ptr<Callback> callback) {
  callback_ = std::move(callback);
  if (callback_ == nullptr) {
    return;
  }

  for (const auto &key_value : config_pmc_->get_all()) {
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
  if (set_option(name, PSLICE() << 'I' << value)) {
    on_option_updated(name);
  }
}

void ConfigShared::set_option_string(Slice name, Slice value) {
  if (set_option(name, PSLICE() << 'S' << value)) {
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
  LOG(ERROR) << "Found \"" << value << "\" instead of boolean option " << name;
  return default_value;
}

int64 ConfigShared::get_option_integer(Slice name, int64 default_value) const {
  auto value = get_option(name);
  if (value.empty()) {
    return default_value;
  }
  if (value[0] != 'I') {
    LOG(ERROR) << "Found \"" << value << "\" instead of integer option " << name;
    return default_value;
  }
  return to_integer<int64>(value.substr(1));
}

string ConfigShared::get_option_string(Slice name, string default_value) const {
  auto value = get_option(name);
  if (value.empty()) {
    return default_value;
  }
  if (value[0] != 'S') {
    LOG(ERROR) << "Found \"" << value << "\" instead of string option " << name;
    return default_value;
  }
  return value.substr(1);
}

bool ConfigShared::set_option(Slice name, Slice value) {
  if (value.empty()) {
    return config_pmc_->erase(name.str()) != 0;
  } else {
    return config_pmc_->set(name.str(), value.str()) != 0;
  }
}

void ConfigShared::on_option_updated(Slice name) const {
  if (callback_ != nullptr) {
    callback_->on_option_updated(name.str(), get_option(name));
  }
}

}  // namespace td
