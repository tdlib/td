//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"

#include "td/utils/common.h"

namespace td {

class Td;

class WidgetManager final : public Actor {
 public:
  WidgetManager(Td *td, ActorShared<> parent);
  WidgetManager(const WidgetManager &) = delete;
  WidgetManager &operator=(const WidgetManager &) = delete;
  WidgetManager(WidgetManager &&) = delete;
  WidgetManager &operator=(WidgetManager &&) = delete;
  ~WidgetManager() final;

 private:
  void tear_down() final;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
