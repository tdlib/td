//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/actor/actor.h"

#include "td/utils/logging.h"

class Worker : public td::Actor {
 public:
  void ping(int x) {
    LOG(ERROR) << "got ping " << x;
  }
};

class MainActor : public td::Actor {
 public:
  void start_up() override {
    LOG(ERROR) << "start up";
    set_timeout_in(10);
    worker_ = td::create_actor_on_scheduler<Worker>("Worker", 1);
    send_closure(worker_, &Worker::ping, 123);
  }

  void timeout_expired() override {
    LOG(ERROR) << "timeout expired";
    td::Scheduler::instance()->finish();
  }

 private:
  td::ActorOwn<Worker> worker_;
};

int main(void) {
  td::ConcurrentScheduler scheduler;
  scheduler.init(4 /*threads_count*/);
  scheduler.start();
  {
    auto guard = scheduler.get_current_guard();
    td::create_actor_on_scheduler<MainActor>("Main actor", 0).release();
  }
  while (!scheduler.is_finished()) {
    scheduler.run_main(10);
  }
  scheduler.finish();
  return 0;
}
