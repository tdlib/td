//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/logging.h"

namespace td {

class Condition {
  class Helper : public Actor {
   public:
    void wait(Promise<> promise) {
      pending_promises_.push_back(std::move(promise));
    }

   private:
    std::vector<Promise<>> pending_promises_;
    void tear_down() override {
      for (auto &promise : pending_promises_) {
        promise.set_value(Unit());
      }
    }
  };

 public:
  Condition() {
    own_actor_ = create_actor<Helper>("helper");
    actor_ = own_actor_.get();
  }
  void wait(Promise<> promise) {
    send_closure(actor_, &Helper::wait, std::move(promise));
  }
  void set_true() {
    CHECK(!own_actor_.empty());
    own_actor_.reset();
  }

 private:
  ActorId<Helper> actor_;
  ActorOwn<Helper> own_actor_;
};

}  // namespace td
