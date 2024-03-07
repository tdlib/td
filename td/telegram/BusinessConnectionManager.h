//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/WaitFreeHashMap.h"

namespace td {

class Td;

class BusinessConnectionManager final : public Actor {
 public:
  BusinessConnectionManager(Td *td, ActorShared<> parent);
  BusinessConnectionManager(const BusinessConnectionManager &) = delete;
  BusinessConnectionManager &operator=(const BusinessConnectionManager &) = delete;
  BusinessConnectionManager(BusinessConnectionManager &&) = delete;
  BusinessConnectionManager &operator=(BusinessConnectionManager &&) = delete;
  ~BusinessConnectionManager() final;

  void on_update_bot_business_connect(telegram_api::object_ptr<telegram_api::botBusinessConnection> &&connection);

 private:
  struct BusinessConnection;

  WaitFreeHashMap<string, unique_ptr<BusinessConnection>> business_connections_;

  void tear_down() final;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
