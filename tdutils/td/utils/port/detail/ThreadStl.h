//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/port/config.h"

#ifdef TD_THREAD_STL

#include "td/utils/common.h"
#include "td/utils/invoke.h"
#include "td/utils/port/detail/ThreadIdGuard.h"
#include "td/utils/port/thread_local.h"
#include "td/utils/Slice.h"

#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

namespace td {
namespace detail {
class ThreadStl {
 public:
  ThreadStl() = default;
  ThreadStl(const ThreadStl &other) = delete;
  ThreadStl &operator=(const ThreadStl &other) = delete;
  ThreadStl(ThreadStl &&) = default;
  ThreadStl &operator=(ThreadStl &&) = default;
  ~ThreadStl() {
    join();
  }
  template <class Function, class... Args>
  explicit ThreadStl(Function &&f, Args &&... args) {
    thread_ = std::thread([args = std::make_tuple(decay_copy(std::forward<Function>(f)),
                                                  decay_copy(std::forward<Args>(args))...)]() mutable {
      ThreadIdGuard thread_id_guard;
      invoke_tuple(std::move(args));
      clear_thread_locals();
    });
  }

  void join() {
    if (thread_.joinable()) {
      thread_.join();
    }
  }
  void detach() {
    if (thread_.joinable()) {
      thread_.detach();
    }
  }
  void set_name(CSlice name) {
  }

  static unsigned hardware_concurrency() {
    return std::thread::hardware_concurrency();
  }

  using id = std::thread::id;

 private:
  std::thread thread_;

  template <class T>
  std::decay_t<T> decay_copy(T &&v) {
    return std::forward<T>(v);
  }
};
namespace this_thread_stl = std::this_thread;
}  // namespace detail
}  // namespace td

#endif
