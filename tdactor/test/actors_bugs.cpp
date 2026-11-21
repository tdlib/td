//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/actor/actor.h"
#include "td/actor/ConcurrentScheduler.h"
#include "td/actor/MultiTimeout.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/port/config.h"
#include "td/utils/port/sleep.h"
#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include <atomic>

TEST(MultiTimeout, bug) {
  td::ConcurrentScheduler sched(0, 0);

  sched.start();
  td::unique_ptr<td::MultiTimeout> multi_timeout;
  struct Data {
    td::MultiTimeout *multi_timeout;
  };
  Data data;

  {
    auto guard = sched.get_main_guard();
    multi_timeout = td::make_unique<td::MultiTimeout>("MultiTimeout");
    data.multi_timeout = multi_timeout.get();
    multi_timeout->set_callback([](void *void_data, td::int64 key) {
      auto &data = *static_cast<Data *>(void_data);
      if (key == 1) {
        data.multi_timeout->cancel_timeout(key + 1);
        data.multi_timeout->set_timeout_in(key + 2, 1);
      } else {
        td::Scheduler::instance()->finish();
      }
    });
    multi_timeout->set_callback_data(&data);
    multi_timeout->set_timeout_in(1, 1);
    multi_timeout->set_timeout_in(2, 2);
  }

  while (sched.run_main(10)) {
    // empty
  }
  sched.finish();
}

class TimeoutManager final : public td::Actor {
  static td::int32 count;

 public:
  TimeoutManager() {
    count++;

    test_timeout_.set_callback(on_test_timeout_callback);
    test_timeout_.set_callback_data(static_cast<void *>(this));
  }
  TimeoutManager(const TimeoutManager &) = delete;
  TimeoutManager &operator=(const TimeoutManager &) = delete;
  TimeoutManager(TimeoutManager &&) = delete;
  TimeoutManager &operator=(TimeoutManager &&) = delete;
  ~TimeoutManager() final {
    count--;
    LOG(INFO) << "Destroy TimeoutManager";
  }

  static void on_test_timeout_callback(void *timeout_manager_ptr, td::int64 id) {
    CHECK(count >= 0);
    if (count == 0) {
      LOG(ERROR) << "Receive timeout after manager was closed";
      return;
    }

    auto manager = static_cast<TimeoutManager *>(timeout_manager_ptr);
    send_closure_later(manager->actor_id(manager), &TimeoutManager::test_timeout);
  }

  void test_timeout() {
    CHECK(count > 0);
    // we must yield scheduler, so run_main breaks immediately, if timeouts are handled immediately
    td::Scheduler::instance()->yield();
  }

  td::MultiTimeout test_timeout_{"TestTimeout"};
};

td::int32 TimeoutManager::count;

TEST(MultiTimeout, Destroy) {
  td::ConcurrentScheduler sched(0, 0);

  auto timeout_manager = sched.create_actor_unsafe<TimeoutManager>(0, "TimeoutManager");
  TimeoutManager *manager = timeout_manager.get().get_actor_unsafe();
  sched.start();
  int cnt = 100;
  while (sched.run_main(cnt == 100 || cnt <= 0 ? 0.001 : 10)) {
    auto guard = sched.get_main_guard();
    cnt--;
    if (cnt > 0) {
      for (int i = 0; i < 2; i++) {
        manager->test_timeout_.set_timeout_in(td::Random::fast(0, 1000000000), td::Random::fast(2, 5) / 1000.0);
      }
    } else if (cnt == 0) {
      timeout_manager.reset();
    } else if (cnt == -10) {
      td::Scheduler::instance()->finish();
    }
  }
  sched.finish();
}

class CountedActor final : public td::Actor {
 public:
  static td::int32 count;

  CountedActor() {
    count++;
  }
  CountedActor(const CountedActor &) = delete;
  CountedActor &operator=(const CountedActor &) = delete;
  CountedActor(CountedActor &&other) noexcept : td::Actor(std::move(other)) {
    count++;
  }
  CountedActor &operator=(CountedActor &&) = delete;
  ~CountedActor() final {
    count--;
  }
};

td::int32 CountedActor::count;

class CreateActorInDestructor final : public td::Actor {
 public:
  explicit CreateActorInDestructor(td::int32 sched_id) : sched_id_(sched_id) {
  }
  CreateActorInDestructor(const CreateActorInDestructor &) = delete;
  CreateActorInDestructor &operator=(const CreateActorInDestructor &) = delete;
  CreateActorInDestructor(CreateActorInDestructor &&) = delete;
  CreateActorInDestructor &operator=(CreateActorInDestructor &&) = delete;
  ~CreateActorInDestructor() final {
    td::create_actor_on_scheduler<CountedActor>("CountedActor", sched_id_).release();
  }

 private:
  td::int32 sched_id_;
};

TEST(Actors, create_actor_during_finish) {
  for (auto is_started : {false, true}) {
    for (td::int32 sched_id = 0; sched_id < 2; sched_id++) {
      td::ConcurrentScheduler sched(1, 0);
      sched.create_actor_unsafe<CreateActorInDestructor>(0, "CreateActorInDestructor", sched_id).release();
      sched.start();
      if (is_started) {
        // process the Start event, so the actor has no pending events during finish
        sched.run_main(0);
      }
      sched.finish();
      ASSERT_EQ(0, CountedActor::count);
    }
  }
}

#if !TD_THREAD_UNSUPPORTED && !TD_EVENTFD_UNSUPPORTED
class CountedActorReceiver final : public td::Actor {
 public:
  void receive(CountedActor actor) {
  }
};

class MigrateDuringFinish final : public td::Actor {
 public:
  static std::atomic<bool> is_started;

  MigrateDuringFinish(td::ConcurrentScheduler *sched, td::ActorId<CountedActorReceiver> receiver, bool send_actor)
      : sched_(sched), receiver_(receiver), send_actor_(send_actor) {
  }

  void start_up() final {
    is_started = true;
    while (!sched_->is_finished()) {
      td::usleep_for(1000);
    }
    // the main scheduler doesn't run anymore, so the migrated actor is received only during finish
    if (send_actor_) {
      CountedActor actor;
      td::register_actor("CountedActor", &actor).release();
      td::send_closure(receiver_, &CountedActorReceiver::receive, std::move(actor));
    } else {
      td::create_actor_on_scheduler<CountedActor>("CountedActor", 0).release();
    }
  }

 private:
  td::ConcurrentScheduler *sched_;
  td::ActorId<CountedActorReceiver> receiver_;
  bool send_actor_;
};

std::atomic<bool> MigrateDuringFinish::is_started;

TEST(Actors, migrate_actor_during_finish) {
  for (auto send_actor : {false, true}) {
    MigrateDuringFinish::is_started = false;
    td::ConcurrentScheduler sched(1, 0);
    auto receiver = sched.create_actor_unsafe<CountedActorReceiver>(0, "CountedActorReceiver").release();
    sched.create_actor_unsafe<MigrateDuringFinish>(1, "MigrateDuringFinish", &sched, receiver, send_actor).release();
    sched.start();
    while (!MigrateDuringFinish::is_started) {
      td::usleep_for(1000);
    }
    sched.finish();
    ASSERT_EQ(0, CountedActor::count);
  }
}
#endif
