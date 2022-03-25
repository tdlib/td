//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"

namespace td {

class Td;

class AttachMenuManager final : public Actor {
 public:
  AttachMenuManager(Td *td, ActorShared<> parent);

  void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const;

 private:

  void tear_down() final;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
