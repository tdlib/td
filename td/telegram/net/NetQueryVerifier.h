//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/NetQuery.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/Promise.h"

#include <utility>

namespace td {

class NetQueryVerifier final : public Actor {
 public:
  explicit NetQueryVerifier(ActorShared<> parent) : parent_(std::move(parent)) {
  }

  void verify(NetQueryPtr query, string nonce);

  void set_verification_token(int64 query_id, string &&token, Promise<Unit> &&promise);

 private:
  void tear_down() final;

  ActorShared<> parent_;

  FlatHashMap<int64, std::pair<NetQueryPtr, string>> queries_;
  int64 next_query_id_ = 1;
};

}  // namespace td
