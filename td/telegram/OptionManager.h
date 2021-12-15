//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"

#include "td/actor/actor.h"

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

  void on_option_updated(const string &name);

  static void clear_options();

  static void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates);

 private:
  void tear_down() final;

  static bool is_internal_option(Slice name);

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
