//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/actor/impl2/ActorLocker.h"
#include "td/actor/impl2/Scheduler.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/port/thread.h"
#include "td/utils/Slice.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tests.h"
#include "td/utils/Time.h"

#include <array>
#include <atomic>
#include <deque>
#include <memory>

using td::actor2::ActorLocker;
using td::actor2::ActorSignals;
using td::actor2::ActorState;
using td::actor2::SchedulerId;

TEST(Actor2, signals) {
  ActorSignals signals;
  signals.add_signal(ActorSignals::Wakeup);
  signals.add_signal(ActorSignals::Cpu);
  signals.add_signal(ActorSignals::Kill);
  signals.clear_signal(ActorSignals::Cpu);

  bool was_kill = false;
  bool was_wakeup = false;
  while (!signals.empty()) {
    auto s = signals.first_signal();
    if (s == ActorSignals::Kill) {
      was_kill = true;
    } else if (s == ActorSignals::Wakeup) {
      was_wakeup = true;
    } else {
      UNREACHABLE();
    }
    signals.clear_signal(s);
  }
  CHECK(was_kill && was_wakeup);
}

TEST(Actors2, flags) {
  ActorState::Flags flags;
  CHECK(!flags.is_locked());
  flags.set_locked(true);
  CHECK(flags.is_locked());
  flags.set_locked(false);
  CHECK(!flags.is_locked());
  flags.set_pause(true);

  flags.set_scheduler_id(SchedulerId{123});

  auto signals = flags.get_signals();
  CHECK(signals.empty());
  signals.add_signal(ActorSignals::Cpu);
  signals.add_signal(ActorSignals::Kill);
  CHECK(signals.has_signal(ActorSignals::Cpu));
  CHECK(signals.has_signal(ActorSignals::Kill));
  flags.set_signals(signals);
  CHECK(flags.get_signals().raw() == signals.raw()) << flags.get_signals().raw() << " " << signals.raw();

  auto wakeup = ActorSignals{};
  wakeup.add_signal(ActorSignals::Wakeup);

  flags.add_signals(wakeup);
  signals.add_signal(ActorSignals::Wakeup);
  CHECK(flags.get_signals().raw() == signals.raw());

  flags.clear_signals();
  CHECK(flags.get_signals().empty());

  CHECK(flags.get_scheduler_id().value() == 123);
  CHECK(flags.is_pause());
}

TEST(Actor2, locker) {
  ActorState state;

  ActorSignals kill_signal;
  kill_signal.add_signal(ActorSignals::Kill);

  ActorSignals wakeup_signal;
  kill_signal.add_signal(ActorSignals::Wakeup);

  ActorSignals cpu_signal;
  kill_signal.add_signal(ActorSignals::Cpu);

  {
    ActorLocker lockerA(&state);
    ActorLocker lockerB(&state);
    ActorLocker lockerC(&state);

    CHECK(lockerA.try_lock());
    CHECK(lockerA.own_lock());
    auto flagsA = lockerA.flags();
    CHECK(lockerA.try_unlock(flagsA));
    CHECK(!lockerA.own_lock());

    CHECK(lockerA.try_lock());
    CHECK(!lockerB.try_lock());
    CHECK(!lockerC.try_lock());

    CHECK(lockerB.try_add_signals(kill_signal));
    CHECK(!lockerC.try_add_signals(wakeup_signal));
    CHECK(lockerC.try_add_signals(wakeup_signal));
    CHECK(!lockerC.add_signals(cpu_signal));
    CHECK(!lockerA.flags().has_signals());
    CHECK(!lockerA.try_unlock(lockerA.flags()));
    {
      auto flags = lockerA.flags();
      auto signals = flags.get_signals();
      bool was_kill = false;
      bool was_wakeup = false;
      bool was_cpu = false;
      while (!signals.empty()) {
        auto s = signals.first_signal();
        if (s == ActorSignals::Kill) {
          was_kill = true;
        } else if (s == ActorSignals::Wakeup) {
          was_wakeup = true;
        } else if (s == ActorSignals::Cpu) {
          was_cpu = true;
        } else {
          UNREACHABLE();
        }
        signals.clear_signal(s);
      }
      CHECK(was_kill && was_wakeup && was_cpu);
      flags.clear_signals();
      CHECK(lockerA.try_unlock(flags));
    }
  }

  {
    ActorLocker lockerB(&state);
    CHECK(lockerB.try_lock());
    CHECK(lockerB.try_unlock(lockerB.flags()));
    CHECK(lockerB.add_signals(kill_signal));
    CHECK(lockerB.flags().get_signals().has_signal(ActorSignals::Kill));
    auto flags = lockerB.flags();
    flags.clear_signals();
    ActorLocker lockerA(&state);
    CHECK(!lockerA.add_signals(kill_signal));
    CHECK(!lockerB.try_unlock(flags));
    CHECK(!lockerA.add_signals(kill_signal));  // do not loose this signal!
    CHECK(!lockerB.try_unlock(flags));
    CHECK(lockerB.flags().get_signals().has_signal(ActorSignals::Kill));
    CHECK(lockerB.try_unlock(flags));
  }

  {
    ActorLocker lockerA(&state);
    CHECK(lockerA.try_lock());
    auto flags = lockerA.flags();
    flags.set_pause(true);
    CHECK(lockerA.try_unlock(flags));
    //We have to lock, though we can't execute.
    CHECK(lockerA.add_signals(wakeup_signal));
  }
}

#if !TD_THREAD_UNSUPPORTED
TEST(Actor2, locker_stress) {
  ActorState state;

  constexpr size_t threads_n = 5;
  auto stage = [&](std::atomic<int> &value, int need) {
    value.fetch_add(1, std::memory_order_release);
    while (value.load(std::memory_order_acquire) < need) {
      td::this_thread::yield();
    }
  };

  struct Node {
    std::atomic<td::uint32> request{0};
    td::uint32 response = 0;
    char pad[64];
  };
  std::array<Node, threads_n> nodes;
  auto do_work = [&]() {
    for (auto &node : nodes) {
      auto query = node.request.load(std::memory_order_acquire);
      if (query) {
        node.response = query * query;
        node.request.store(0, std::memory_order_relaxed);
      }
    }
  };

  std::atomic<int> begin{0};
  std::atomic<int> ready{0};
  std::atomic<int> check{0};
  std::atomic<int> finish{0};
  std::vector<td::thread> threads;
  for (size_t i = 0; i < threads_n; i++) {
    threads.push_back(td::thread([&, id = i] {
      for (size_t i = 1; i < 1000000; i++) {
        ActorLocker locker(&state);
        auto need = static_cast<int>(threads_n * i);
        auto query = static_cast<td::uint32>(id + need);
        stage(begin, need);
        nodes[id].request = 0;
        nodes[id].response = 0;
        stage(ready, need);
        if (locker.try_lock()) {
          nodes[id].response = query * query;
        } else {
          auto cpu = ActorSignals::one(ActorSignals::Cpu);
          nodes[id].request.store(query, std::memory_order_release);
          locker.add_signals(cpu);
        }
        while (locker.own_lock()) {
          auto flags = locker.flags();
          auto signals = flags.get_signals();
          if (!signals.empty()) {
            do_work();
          }
          flags.clear_signals();
          locker.try_unlock(flags);
        }

        stage(check, need);
        if (id == 0) {
          CHECK(locker.add_signals(ActorSignals{}));
          CHECK(!locker.flags().has_signals());
          CHECK(locker.try_unlock(locker.flags()));
          for (size_t thread_id = 0; thread_id < threads_n; thread_id++) {
            CHECK(nodes[thread_id].response ==
                  static_cast<td::uint32>(thread_id + need) * static_cast<td::uint32>(thread_id + need))
                << td::tag("thread", thread_id) << " " << nodes[thread_id].response << " "
                << nodes[thread_id].request.load();
          }
        }
      }
    }));
  }
  for (auto &thread : threads) {
    thread.join();
  }
}

namespace {
const size_t BUF_SIZE = 1024 * 1024;
char buf[BUF_SIZE];
td::StringBuilder sb(td::MutableSlice(buf, BUF_SIZE - 1));
}  // namespace

TEST(Actor2, executor_simple) {
  using namespace td;
  using namespace td::actor2;
  struct Dispatcher : public SchedulerDispatcher {
    void add_to_queue(ActorInfoPtr ptr, SchedulerId scheduler_id, bool need_poll) override {
      queue.push_back(std::move(ptr));
    }
    void set_alarm_timestamp(const ActorInfoPtr &actor_info_ptr, Timestamp timestamp) override {
      UNREACHABLE();
    }
    SchedulerId get_scheduler_id() const override {
      return SchedulerId{0};
    }
    std::deque<ActorInfoPtr> queue;
  };
  Dispatcher dispatcher;

  class TestActor : public Actor {
   public:
    void close() {
      stop();
    }

   private:
    void start_up() override {
      sb << "StartUp";
    }
    void tear_down() override {
      sb << "TearDown";
    }
  };
  ActorInfoCreator actor_info_creator;
  auto actor = actor_info_creator.create(
      std::make_unique<TestActor>(), ActorInfoCreator::Options().on_scheduler(SchedulerId{0}).with_name("TestActor"));
  dispatcher.add_to_queue(actor, SchedulerId{0}, false);

  {
    ActorExecutor executor(*actor, dispatcher, ActorExecutor::Options());
    CHECK(executor.can_send());
    CHECK(executor.can_send_immediate());
    CHECK(sb.as_cslice() == "StartUp") << sb.as_cslice();
    sb.clear();
    executor.send(ActorMessageCreator::lambda([&] { sb << "A"; }));
    CHECK(sb.as_cslice() == "A") << sb.as_cslice();
    sb.clear();
    auto big_message = ActorMessageCreator::lambda([&] { sb << "big"; });
    big_message.set_big();
    executor.send(std::move(big_message));
    CHECK(sb.as_cslice() == "") << sb.as_cslice();
    executor.send(ActorMessageCreator::lambda([&] { sb << "A"; }));
    CHECK(sb.as_cslice() == "") << sb.as_cslice();
  }
  CHECK(dispatcher.queue.size() == 1);
  { ActorExecutor executor(*actor, dispatcher, ActorExecutor::Options().with_from_queue()); }
  CHECK(dispatcher.queue.size() == 1);
  dispatcher.queue.clear();
  CHECK(sb.as_cslice() == "bigA") << sb.as_cslice();
  sb.clear();
  {
    ActorExecutor executor(*actor, dispatcher, ActorExecutor::Options());
    executor.send(
        ActorMessageCreator::lambda([&] { static_cast<TestActor &>(ActorExecuteContext::get()->actor()).close(); }));
  }
  CHECK(sb.as_cslice() == "TearDown") << sb.as_cslice();
  sb.clear();
  CHECK(!actor->has_actor());
  {
    ActorExecutor executor(*actor, dispatcher, ActorExecutor::Options());
    executor.send(
        ActorMessageCreator::lambda([&] { static_cast<TestActor &>(ActorExecuteContext::get()->actor()).close(); }));
  }
  CHECK(dispatcher.queue.empty());
  CHECK(sb.as_cslice() == "");
}

using namespace td::actor2;
using td::uint32;
static std::atomic<int> cnt;
class Worker : public Actor {
 public:
  void query(uint32 x, ActorInfoPtr master);
  void close() {
    stop();
  }
};
class Master : public Actor {
 public:
  void on_result(uint32 x, uint32 y) {
    loop();
  }

 private:
  uint32 l = 0;
  uint32 r = 100000;
  ActorInfoPtr worker;
  void start_up() override {
    worker = detail::create_actor<Worker>(ActorOptions().with_name("Master"));
    loop();
  }
  void loop() override {
    l++;
    if (l == r) {
      if (!--cnt) {
        SchedulerContext::get()->stop();
      }
      detail::send_closure(*worker, &Worker::close);
      stop();
      return;
    }
    detail::send_lambda(*worker,
                        [x = l, self = get_actor_info_ptr()] { detail::current_actor<Worker>().query(x, self); });
  }
};

void Worker::query(uint32 x, ActorInfoPtr master) {
  auto y = x;
  for (int i = 0; i < 100; i++) {
    y = y * y;
  }
  detail::send_lambda(*master, [result = y, x] { detail::current_actor<Master>().on_result(x, result); });
}

TEST(Actor2, scheduler_simple) {
  auto group_info = std::make_shared<SchedulerGroupInfo>(1);
  Scheduler scheduler{group_info, SchedulerId{0}, 2};
  scheduler.start();
  scheduler.run_in_context([] {
    cnt = 10;
    for (int i = 0; i < 10; i++) {
      detail::create_actor<Master>(ActorOptions().with_name("Master"));
    }
  });
  while (scheduler.run(1000)) {
  }
  Scheduler::close_scheduler_group(*group_info);
}

TEST(Actor2, actor_id_simple) {
  auto group_info = std::make_shared<SchedulerGroupInfo>(1);
  Scheduler scheduler{group_info, SchedulerId{0}, 2};
  sb.clear();
  scheduler.start();

  scheduler.run_in_context([] {
    class A : public Actor {
     public:
      A(int value) : value_(value) {
        sb << "A" << value_;
      }
      void hello() {
        sb << "hello";
      }
      ~A() {
        sb << "~A";
        if (--cnt <= 0) {
          SchedulerContext::get()->stop();
        }
      }

     private:
      int value_;
    };
    cnt = 1;
    auto id = create_actor<A>("A", 123);
    CHECK(sb.as_cslice() == "A123");
    sb.clear();
    send_closure(id, &A::hello);
  });
  while (scheduler.run(1000)) {
  }
  CHECK(sb.as_cslice() == "hello~A");
  Scheduler::close_scheduler_group(*group_info);
  sb.clear();
}

TEST(Actor2, actor_creation) {
  auto group_info = std::make_shared<SchedulerGroupInfo>(1);
  Scheduler scheduler{group_info, SchedulerId{0}, 1};
  scheduler.start();

  scheduler.run_in_context([]() mutable {
    class B;
    class A : public Actor {
     public:
      void f() {
        check();
        stop();
      }

     private:
      void start_up() override {
        check();
        create_actor<B>("Simple", actor_id(this)).release();
      }

      void check() {
        auto &context = *SchedulerContext::get();
        CHECK(context.has_poll());
        context.get_poll();
      }

      void tear_down() override {
        if (--cnt <= 0) {
          SchedulerContext::get()->stop();
        }
      }
    };

    class B : public Actor {
     public:
      B(ActorId<A> a) : a_(a) {
      }

     private:
      void start_up() override {
        auto &context = *SchedulerContext::get();
        CHECK(!context.has_poll());
        send_closure(a_, &A::f);
        stop();
      }
      void tear_down() override {
        if (--cnt <= 0) {
          SchedulerContext::get()->stop();
        }
      }
      ActorId<A> a_;
    };
    cnt = 2;
    create_actor<A>(ActorOptions().with_name("Poll").with_poll()).release();
  });
  while (scheduler.run(1000)) {
  }
  scheduler.stop();
  Scheduler::close_scheduler_group(*group_info);
}

TEST(Actor2, actor_timeout_simple) {
  auto group_info = std::make_shared<SchedulerGroupInfo>(1);
  Scheduler scheduler{group_info, SchedulerId{0}, 2};
  sb.clear();
  scheduler.start();

  scheduler.run_in_context([] {
    class A : public Actor {
     public:
      void start_up() override {
        set_timeout();
      }
      void alarm() override {
        double diff = td::Time::now() - expected_timeout_;
        CHECK(-0.001 < diff && diff < 0.1) << diff;
        if (cnt_-- > 0) {
          set_timeout();
        } else {
          stop();
        }
      }

      void tear_down() override {
        SchedulerContext::get()->stop();
      }

     private:
      double expected_timeout_;
      int cnt_ = 5;
      void set_timeout() {
        auto wakeup_timestamp = td::Timestamp::in(0.1);
        expected_timeout_ = wakeup_timestamp.at();
        alarm_timestamp() = wakeup_timestamp;
      }
    };
    create_actor<A>(ActorInfoCreator::Options().with_name("A").with_poll()).release();
  });
  while (scheduler.run(1000)) {
  }
  Scheduler::close_scheduler_group(*group_info);
  sb.clear();
}
#endif  //!TD_THREAD_UNSUPPORTED
