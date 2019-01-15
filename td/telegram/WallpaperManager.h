//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

namespace td {

class Td;

class WallpaperManager : public Actor {
 public:
  WallpaperManager(Td *td, ActorShared<> parent);

  void get_wallpapers(Promise<td_api::object_ptr<td_api::wallpapers>> &&promise);

 private:
  void tear_down() override;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
