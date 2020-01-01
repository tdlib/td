//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/NetQuery.h"

#include "td/actor/actor.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/Status.h"

namespace td {

class Td;

class NetActor : public NetQueryCallback {
 public:
  NetActor();
  void set_parent(ActorShared<> parent);
  void on_result(NetQueryPtr query) override;
  virtual void on_result(uint64 id, BufferSlice packet) {
    UNREACHABLE();
  }
  virtual void on_error(uint64 id, Status status) {
    UNREACHABLE();
  }
  virtual void on_result_finish() {
  }

 protected:
  ActorShared<> parent_;
  void send_query(NetQueryPtr query);
  Td *td;
};

class NetActorOnce : public NetActor {
  void hangup() override {
    on_error(0, Status::Error(500, "Request aborted"));
    stop();
  }

  void on_result_finish() override {
    stop();
  }
};

}  // namespace td
