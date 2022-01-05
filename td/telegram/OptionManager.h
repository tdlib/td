//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"

namespace td {

class Td;

class OptionManager final : public Actor {
 public:
  OptionManager(Td *td, ActorShared<> parent);

  OptionManager(const OptionManager &) = delete;
  OptionManager &operator=(const OptionManager &) = delete;
  OptionManager(OptionManager &&) = delete;
  OptionManager &operator=(OptionManager &&) = delete;
  ~OptionManager() final;

  void on_update_server_time_difference();

  void on_option_updated(const string &name);

  void get_option(const string &name, Promise<td_api::object_ptr<td_api::OptionValue>> &&promise);

  void set_option(const string &name, td_api::object_ptr<td_api::OptionValue> &&value, Promise<Unit> &&promise);

  static void clear_options();

  void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const;

 private:
  void tear_down() final;

  static bool is_internal_option(Slice name);

  static td_api::object_ptr<td_api::OptionValue> get_unix_time_option_value_object();

  static td_api::object_ptr<td_api::OptionValue> get_option_value_object(Slice value);

  void send_unix_time_update();

  void set_default_reaction();

  void on_set_default_reaction(bool success);

  Td *td_;
  ActorShared<> parent_;

  double last_sent_server_time_difference_ = 1e100;
};

}  // namespace td
