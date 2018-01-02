//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"

#include <atomic>

namespace td {

class NetQueryCounter {
  static std::atomic<uint64> net_query_cnt_;

 public:
  static uint64 get_count() {
    return net_query_cnt_.load();
  }

  bool empty() const {
    return !is_alive_;
  }

  explicit NetQueryCounter(bool is_alive = false) : is_alive_(is_alive) {
    if (is_alive) {
      net_query_cnt_++;
    }
  }

  NetQueryCounter(const NetQueryCounter &other) = delete;
  NetQueryCounter &operator=(const NetQueryCounter &other) = delete;
  NetQueryCounter(NetQueryCounter &&other) : is_alive_(other.is_alive_) {
    other.is_alive_ = false;
  }
  NetQueryCounter &operator=(NetQueryCounter &&other) {
    if (is_alive_) {
      net_query_cnt_--;
    }
    is_alive_ = other.is_alive_;
    other.is_alive_ = false;
    return *this;
  }
  ~NetQueryCounter() {
    if (is_alive_) {
      net_query_cnt_--;
    }
  }

 private:
  bool is_alive_;
};
}  // namespace td
