//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/port/thread.h"

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace td {

class MpmcWaiter {
 public:
  int wait(int yields, uint32 worker_id) {
    if (yields < RoundsTillSleepy) {
      td::this_thread::yield();
      return yields + 1;
    } else if (yields == RoundsTillSleepy) {
      auto state = state_.load(std::memory_order_relaxed);
      if (!State::has_worker(state)) {
        auto new_state = State::with_worker(state, worker_id);
        if (state_.compare_exchange_strong(state, new_state, std::memory_order_acq_rel)) {
          td::this_thread::yield();
          return yields + 1;
        }
        if (state == State::awake()) {
          return 0;
        }
      }
      td::this_thread::yield();
      return 0;
    } else if (yields < RoundsTillAsleep) {
      auto state = state_.load(std::memory_order_acquire);
      if (State::still_sleepy(state, worker_id)) {
        td::this_thread::yield();
        return yields + 1;
      }
      return 0;
    } else {
      auto state = state_.load(std::memory_order_acquire);
      if (State::still_sleepy(state, worker_id)) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (state_.compare_exchange_strong(state, State::asleep(), std::memory_order_acq_rel)) {
          condition_variable_.wait(lock);
        }
      }
      return 0;
    }
  }

  int stop_wait(int yields, uint32 worker_id) {
    if (yields > RoundsTillSleepy) {
      notify_cold();
    }
    return 0;
  }

  void notify() {
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (state_.load(std::memory_order_acquire) == State::awake()) {
      return;
    }
    notify_cold();
  }

 private:
  struct State {
    static constexpr uint32 awake() {
      return 0;
    }
    static constexpr uint32 asleep() {
      return 1;
    }
    static bool is_asleep(uint32 state) {
      return (state & 1) != 0;
    }
    static bool has_worker(uint32 state) {
      return (state >> 1) != 0;
    }
    static int32 with_worker(uint32 state, uint32 worker) {
      return state | ((worker + 1) << 1);
    }
    static bool still_sleepy(uint32 state, uint32 worker) {
      return (state >> 1) == (worker + 1);
    }
  };
  //enum { RoundsTillSleepy = 32, RoundsTillAsleep = 64 };
  enum { RoundsTillSleepy = 1, RoundsTillAsleep = 2 };
  std::atomic<uint32> state_{State::awake()};
  std::mutex mutex_;
  std::condition_variable condition_variable_;

  void notify_cold() {
    auto old_state = state_.exchange(State::awake(), std::memory_order_release);
    if (State::is_asleep(old_state)) {
      std::lock_guard<std::mutex> guard(mutex_);
      condition_variable_.notify_all();
    }
  }
};

}  // namespace td
