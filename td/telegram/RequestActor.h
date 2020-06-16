//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/Td.h"

#include "td/telegram/td_api.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"

#include <type_traits>

namespace td {

template <class T = Unit>
class RequestActor : public Actor {
 public:
  RequestActor(ActorShared<Td> td_id, uint64 request_id)
      : td_id_(std::move(td_id)), td(td_id_.get().get_actor_unsafe()), request_id_(request_id) {
  }

  void loop() override {
    PromiseActor<T> promise_actor;
    FutureActor<T> future;
    init_promise_future(&promise_actor, &future);

    auto promise = PromiseCreator::from_promise_actor(std::move(promise_actor));
    do_run(std::move(promise));

    if (future.is_ready()) {
      CHECK(!promise);
      if (future.is_error()) {
        do_send_error(future.move_as_error());
      } else {
        do_set_result(future.move_as_ok());
        do_send_result();
      }
      stop();
    } else {
      CHECK(!future.empty());
      CHECK(future.get_state() == FutureActor<T>::State::Waiting);
      if (--tries_left_ == 0) {
        future.close();
        do_send_error(Status::Error(400, "Requested data is inaccessible"));
        return stop();
      }

      future.set_event(EventCreator::raw(actor_id(), nullptr));
      future_ = std::move(future);
    }
  }

  void raw_event(const Event::Raw &event) override {
    if (future_.is_error()) {
      auto error = future_.move_as_error();
      if (error == Status::Error<FutureActor<T>::HANGUP_ERROR_CODE>()) {
        // dropping query due to lost authorization or lost promise
        // td may be already closed, so we should check is auth_manager_ is empty
        bool is_authorized = td->auth_manager_ && td->auth_manager_->is_authorized();
        if (is_authorized) {
          LOG(ERROR) << "Promise was lost";
          do_send_error(Status::Error(500, "Query can't be answered due to bug in the TDLib"));
        } else {
          do_send_error(Status::Error(401, "Unauthorized"));
        }
        return stop();
      }

      do_send_error(std::move(error));
      stop();
    } else {
      do_set_result(future_.move_as_ok());
      loop();
    }
  }

  void on_start_migrate(int32 /*sched_id*/) override {
    UNREACHABLE();
  }
  void on_finish_migrate() override {
    UNREACHABLE();
  }

  int get_tries() const {
    return tries_left_;
  }

  void set_tries(int32 tries) {
    tries_left_ = tries;
  }

 protected:
  ActorShared<Td> td_id_;
  Td *td;

  void send_result(tl_object_ptr<td_api::Object> &&result) {
    send_closure(td_id_, &Td::send_result, request_id_, std::move(result));
  }

  void send_error(Status &&status) {
    LOG(INFO) << "Receive error for query: " << status;
    send_closure(td_id_, &Td::send_error, request_id_, std::move(status));
  }

 private:
  virtual void do_run(Promise<T> &&promise) = 0;

  virtual void do_send_result() {
    send_result(make_tl_object<td_api::ok>());
  }

  virtual void do_send_error(Status &&status) {
    send_error(std::move(status));
  }

  virtual void do_set_result(T &&result) {
    CHECK((std::is_same<T, Unit>::value));  // all other results should be implicitly handled by overriding this method
  }

  void hangup() override {
    do_send_error(Status::Error(500, "Request aborted"));
    stop();
  }

  friend class RequestOnceActor;

  uint64 request_id_;
  int tries_left_ = 2;
  FutureActor<T> future_;
};

class RequestOnceActor : public RequestActor<> {
 public:
  RequestOnceActor(ActorShared<Td> td_id, uint64 request_id) : RequestActor(std::move(td_id), request_id) {
  }

  void loop() override {
    if (get_tries() < 2) {
      do_send_result();
      stop();
      return;
    }

    RequestActor::loop();
  }
};

}  // namespace td
