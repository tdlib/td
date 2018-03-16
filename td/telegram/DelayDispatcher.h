//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once
#include "td/telegram/net/NetQuery.h"

#include "td/actor/actor.h"
#include "td/utils/Time.h"

#include <queue>

namespace td {
class DelayDispatcher : public Actor {
 public:
  void send_with_callback(NetQueryPtr query, ActorShared<NetQueryCallback> callback);

 private:
  struct Query {
    NetQueryPtr net_query;
    ActorShared<NetQueryCallback> callback;
  };
  std::queue<Query> queue_;
  Timestamp wakeup_at_;
  static constexpr double DELAY = 0.000;

  void loop() override;
};
}  // namespace td
