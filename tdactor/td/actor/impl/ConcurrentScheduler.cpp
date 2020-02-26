//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
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
#if !TD_THREAD_UNSUPPORTED && !TD_EVENTFD_UNSUPPORTED
  for (int32 i = 0; i < threads_n; i++) {
    auto queue = std::make_shared<MpscPollableQueue<EventFull>>();
    queue->init();
    outbound[i] = queue;
  }
#endif

  // +1 for extra scheduler for IOCP and send_closure from unrelated threads
  // It will know about other schedulers
  // Other schedulers will have no idea about its existence
  extra_scheduler_ = 1;
#if TD_THREAD_UNSUPPORTED || TD_EVENTFD_UNSUPPORTED
  extra_scheduler_ = 0;
#endif

  schedulers_.resize(threads_n + extra_scheduler_);
  for (int32 i = 0; i < threads_n + extra_scheduler_; i++) {
    auto &sched = schedulers_[i];
    sched = make_unique<Scheduler>();

#if !TD_THREAD_UNSUPPORTED && !TD_EVENTFD_UNSUPPORTED
    if (i >= threads_n) {
      auto queue = std::make_shared<MpscPollableQueue<EventFull>>();
      queue->init();
      outbound.push_back(std::move(queue));
    }
#endif

    sched->init(i, outbound, static_cast<Scheduler::Callback *>(this));
  }

#if TD_PORT_WINDOWS
  iocp_ = make_unique<detail::Iocp>();
  iocp_->init();
#endif

  state_ = State::Start;
}

void ConcurrentScheduler::test_one_thread_run() {
  do {
    for (auto &sched : schedulers_) {
      sched->run(Timestamp::now_cached());
    }
  } while (!is_finished_.load(std::memory_order_relaxed));
}

void ConcurrentScheduler::start() {
  CHECK(state_ == State::Start);
  is_finished_.store(false, std::memory_order_relaxed);
#if !TD_THREAD_UNSUPPORTED && !TD_EVENTFD_UNSUPPORTED
  for (size_t i = 1; i + extra_scheduler_ < schedulers_.size(); i++) {
    auto &sched = schedulers_[i];
    threads_.push_back(td::thread([&] {
#if TD_PORT_WINDOWS
      detail::Iocp::Guard iocp_guard(iocp_.get());
#endif
      while (!is_finished()) {
        sched->run(Timestamp::in(10));
      }
    }));
  }
#if TD_PORT_WINDOWS
  iocp_thread_ = td::thread([this] {
    auto guard = this->get_send_guard();
    this->iocp_->loop();
  });
#endif
#endif

  state_ = State::Run;
}
static TD_THREAD_LOCAL double emscripten_timeout;

bool ConcurrentScheduler::run_main(Timestamp timeout) {
  CHECK(state_ == State::Run);
  // run main scheduler in same thread
  auto &main_sched = schedulers_[0];
  if (!is_finished()) {
#if TD_PORT_WINDOWS
    detail::Iocp::Guard iocp_guard(iocp_.get());
#endif
    main_sched->run(timeout);
  }

  // hack for emscripten
  emscripten_timeout = get_main_timeout().at();

  return !is_finished();
}

Timestamp ConcurrentScheduler::get_main_timeout() {
  CHECK(state_ == State::Run);
  return schedulers_[0]->get_timeout();
}

double ConcurrentScheduler::emscripten_get_main_timeout() {
  return Timestamp::at(emscripten_timeout).in();
}
void ConcurrentScheduler::emscripten_clear_main_timeout() {
  emscripten_timeout = 0;
}

void ConcurrentScheduler::finish() {
  CHECK(state_ == State::Run);
  if (!is_finished()) {
    on_finish();
  }
#if TD_PORT_WINDOWS
  SCOPE_EXIT {
    iocp_->clear();
  };
  detail::Iocp::Guard iocp_guard(iocp_.get());
#endif

#if !TD_THREAD_UNSUPPORTED && !TD_EVENTFD_UNSUPPORTED
  for (auto &thread : threads_) {
    thread.join();
  }
  threads_.clear();
#endif

#if TD_PORT_WINDOWS
  iocp_->interrupt_loop();
  iocp_thread_.join();
#endif

  schedulers_.clear();
  for (auto &f : at_finish_) {
    f();
  }
  at_finish_.clear();

  state_ = State::Start;
}

}  // namespace td
