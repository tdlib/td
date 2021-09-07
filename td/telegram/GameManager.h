//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"

namespace td {

class Td;

class GameManager final : public Actor {
 public:
  GameManager(Td *td, ActorShared<> parent);
  GameManager(const GameManager &) = delete;
  GameManager &operator=(const GameManager &) = delete;
  GameManager(GameManager &&) = delete;
  GameManager &operator=(GameManager &&) = delete;
  ~GameManager() final;

 private:
  void tear_down() final;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
