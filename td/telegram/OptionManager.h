//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"

#include "td/utils/common.h"

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

 private:
  void tear_down() final;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
