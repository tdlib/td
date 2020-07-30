//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"

#include <atomic>

namespace td {

class NetQueryCounter {
 public:
  using Counter = std::atomic<uint64>;
  // deprecated
  NetQueryCounter(bool is_alive = false) {
    if (is_alive) {
      *this = NetQueryCounter(&counter_);
    }
  }

  static uint64 get_count() {
    return counter_.load();
  }

  NetQueryCounter(Counter *counter) : ptr_(counter) {
    counter->fetch_add(1);
  }

  explicit operator bool() const {
    return (bool)ptr_;
  }

 private:
  struct Deleter {
    void operator()(Counter *ptr) {
      ptr->fetch_sub(1);
    }
  };
  static Counter counter_;
  std::unique_ptr<Counter, Deleter> ptr_;
};

}  // namespace td
