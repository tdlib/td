//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/port/thread.h"
#include "td/utils/port/thread_local.h"
#include "td/utils/int_types.h"

#include <atomic>
#include <array>

namespace td {
template <class T>
class ThreadLocalStorage {
 public:
  T& get() {
    return thread_local_node().value;
  }

  template <class F>
  void for_each(F&& f) {
    int n = max_thread_id_.load();
    for (int i = 0; i < n; i++) {
      f(nodes_[i].value);
    }
  }
  template <class F>
  void for_each(F&& f) const {
    int n = max_thread_id_.load();
    for (int i = 0; i < n; i++) {
      f(nodes_[i].value);
    }
  }

 private:
  struct Node {
    T value{};
    char padding[128];
  };
  static constexpr int MAX_THREAD_ID = 128;
  std::atomic<int> max_thread_id_{MAX_THREAD_ID};
  std::array<Node, MAX_THREAD_ID> nodes_;

  Node& thread_local_node() {
    auto thread_id = get_thread_id();
    CHECK(0 <= thread_id && static_cast<size_t>(thread_id) < nodes_.size());
    return nodes_[thread_id];
  }
};
}  // namespace td
