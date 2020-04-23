//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"

#include "td/db/KeyValueSyncInterface.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"

#include <memory>
#include <unordered_map>

namespace td {

class ConfigShared {
 public:
  class Callback {
   public:
    Callback() = default;
    Callback(const Callback &) = delete;
    Callback &operator=(const Callback &) = delete;
    Callback(Callback &&) = delete;
    Callback &operator=(Callback &&) = delete;
    virtual ~Callback() = default;
    virtual void on_option_updated(const string &name, const string &value) const = 0;
  };

  explicit ConfigShared(std::shared_ptr<KeyValueSyncInterface> config_pmc);

  void set_callback(unique_ptr<Callback> callback);

  void set_option_boolean(Slice name, bool value);
  void set_option_empty(Slice name);
  void set_option_integer(Slice name, int32 value);
  void set_option_string(Slice name, Slice value);

  bool have_option(Slice name) const;
  std::unordered_map<string, string> get_options() const;

  bool get_option_boolean(Slice name, bool default_value = false) const;
  int32 get_option_integer(Slice name, int32 default_value = 0) const;
  string get_option_string(Slice name, string default_value = "") const;

  tl_object_ptr<td_api::OptionValue> get_option_value(Slice name) const;

  static tl_object_ptr<td_api::OptionValue> get_option_value_object(Slice value);

 private:
  std::shared_ptr<KeyValueSyncInterface> config_pmc_;
  unique_ptr<Callback> callback_;

  bool set_option(Slice name, Slice value);

  string get_option(Slice name) const;

  void on_option_updated(Slice name) const;
};

}  // namespace td
