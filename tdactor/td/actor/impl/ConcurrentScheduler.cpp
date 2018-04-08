//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/actor/impl/ConcurrentScheduler.h"

#include "td/actor/impl/Actor.h"
#include "td/actor/impl/ActorId.h"
#include "td/actor/impl/ActorInfo.h"
#include "td/actor/impl/Scheduler.h"

#include "td/utils/MpscPollableQueue.h"
#include "td/utils/port/thread_local.h"

#include <memory>

namespace td {

void ConcurrentScheduler::init(int32 threads_n) {
#if TD_THREAD_UNSUPPORTED || TD_EVENTFD_UNSUPPORTED
  threads_n = 0;
#endif
  threads_n++;
  std::vector<std::shared_ptr<MpscPollableQueue<EventFull>>> outbound(threads_n);
  for (int32 i = 0; i < threads_n; i++) {
#if TD_THREAD_UNSUPPORTED || TD_EVENTFD_UNSUPPORTED
#else
    auto queue = std::make_shared<MpscPollableQueue<EventFull>>();
    queue->init();
    outbound[i] = queue;
#endif
  }

  schedulers_.resize(threads_n);
  for (int32 i = 0; i < threads_n; i++) {
    auto &sched = schedulers_[i];
    sched = make_unique<Scheduler>();
    sched->init(i, outbound, static_cast<Scheduler::Callback *>(this));
  }

  state_ = State::Start;
}

void ConcurrentScheduler::test_one_thread_run() {
  do {
    for (auto &sched : schedulers_) {
      sched->run(0);
    }
  } while (!is_finished_.load(std::memory_order_relaxed));
}

void ConcurrentScheduler::start() {
  CHECK(state_ == State::Start);
  is_finished_.store(false, std::memory_order_relaxed);
  set_thread_id(0);
#if !TD_THREAD_UNSUPPORTED && !TD_EVENTFD_UNSUPPORTED
  for (size_t i = 1; i < schedulers_.size(); i++) {
    auto &sched = schedulers_[i];
    threads_.push_back(td::thread([&, tid = i]() {
      set_thread_id(static_cast<int32>(tid));
      while (!is_finished()) {
        sched->run(10);
      }
    }));
  }
#endif
  state_ = State::Run;
}

bool ConcurrentScheduler::run_main(double timeout) {
  CHECK(state_ == State::Run);
  // run main scheduler in same thread
  auto &main_sched = schedulers_[0];
  if (!is_finished()) {
    main_sched->run(timeout);
  }
  return !is_finished();
}

void ConcurrentScheduler::finish() {
  CHECK(state_ == State::Run);
  if (!is_finished()) {
    on_finish();
  }
#if !TD_THREAD_UNSUPPORTED && !TD_EVENTFD_UNSUPPORTED
  for (auto &thread : threads_) {
    thread.join();
  }
  threads_.clear();
#endif
  schedulers_.clear();
  for (auto &f : at_finish_) {
    f();
  }
  at_finish_.clear();

  state_ = State::Start;
}

}  // namespace td
