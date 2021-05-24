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

class LinkManager : public Actor {
 public:
  LinkManager(Td *td, ActorShared<> parent);

  LinkManager(const LinkManager &) = delete;
  LinkManager &operator=(const LinkManager &) = delete;
  LinkManager(LinkManager &&) = delete;
  LinkManager &operator=(LinkManager &&) = delete;
  ~LinkManager() override;

 private:
  void tear_down() override;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
