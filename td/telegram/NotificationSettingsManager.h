//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"

#include "td/utils/common.h"

namespace td {

class Td;

class NotificationSettingsManager final : public Actor {
 public:
  NotificationSettingsManager(Td *td, ActorShared<> parent);
  NotificationSettingsManager(const NotificationSettingsManager &) = delete;
  NotificationSettingsManager &operator=(const NotificationSettingsManager &) = delete;
  NotificationSettingsManager(NotificationSettingsManager &&) = delete;
  NotificationSettingsManager &operator=(NotificationSettingsManager &&) = delete;
  ~NotificationSettingsManager() final;

 private:
  struct SponsoredMessage;
  struct DialogSponsoredMessages;

  void tear_down() final;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
