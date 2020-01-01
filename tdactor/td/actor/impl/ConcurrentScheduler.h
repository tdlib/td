//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/impl/Scheduler-decl.h"

#include "td/utils/common.h"
#include "td/utils/port/thread.h"
#include "td/utils/Slice.h"
#include "td/utils/Time.h"

#if TD_PORT_WINDOWS
#include "td/utils/port/detail/Iocp.h"
#endif

#include <atomic>
#include <functional>
#include <mutex>
#include <utility>

namespace td {

class ConcurrentScheduler : private Scheduler::Callback {
 public:
  void init(int32 threads_n);

  void finish_async() {
    schedulers_[0]->finish();
  }
  void wakeup() {
    schedulers_[0]->wakeup();
  }
  SchedulerGuard get_main_guard() {
    return schedulers_[0]->get_guard();
  }

  SchedulerGuard get_send_guard() {
    return schedulers_.back()->get_const_guard();
  }

  void test_one_thread_run();

  bool is_finished() {
    return is_finished_.load(std::memory_order_relaxed);
  }

  void start();

  bool run_main(double timeout) {
    return run_main(Timestamp::in(timeout));
  }
  bool run_main(Timestamp timeout);

  Timestamp get_main_timeout();
  static double emscripten_get_main_timeout();
  static void emscripten_clear_main_timeout();

  void finish();

  template <class ActorT, class... Args>
  ActorOwn<ActorT> create_actor_unsafe(int32 sched_id, Slice name, Args &&... args) {
#if TD_THREAD_UNSUPPORTED || TD_EVENTFD_UNSUPPORTED
    sched_id = 0;
#endif
    CHECK(0 <= sched_id && sched_id < static_cast<int32>(schedulers_.size()));
    auto guard = schedulers_[sched_id]->get_guard();
    return schedulers_[sched_id]->create_actor<ActorT>(name, std::forward<Args>(args)...);
  }

  template <class ActorT>
  ActorOwn<ActorT> register_actor_unsafe(int32 sched_id, Slice name, ActorT *actor) {
#if TD_THREAD_UNSUPPORTED || TD_EVENTFD_UNSUPPORTED
    sched_id = 0;
#endif
    CHECK(0 <= sched_id && sched_id < static_cast<int32>(schedulers_.size()));
    auto guard = schedulers_[sched_id]->get_guard();
    return schedulers_[sched_id]->register_actor<ActorT>(name, actor);
  }

 private:
  enum class State { Start, Run };
  State state_ = State::Start;
  std::vector<unique_ptr<Scheduler>> schedulers_;
  std::atomic<bool> is_finished_{false};
  std::mutex at_finish_mutex_;
  std::vector<std::function<void()>> at_finish_;
#if !TD_THREAD_UNSUPPORTED && !TD_EVENTFD_UNSUPPORTED
  std::vector<thread> threads_;
#endif
#if TD_PORT_WINDOWS
  unique_ptr<detail::Iocp> iocp_;
  td::thread iocp_thread_;
#endif
  int32 extra_scheduler_;

  void on_finish() override {
    is_finished_.store(true, std::memory_order_relaxed);
    for (auto &it : schedulers_) {
      it->wakeup();
    }
  }

  void register_at_finish(std::function<void()> f) override {
    std::lock_guard<std::mutex> lock(at_finish_mutex_);
    at_finish_.push_back(std::move(f));
  }
};

}  // namespace td
