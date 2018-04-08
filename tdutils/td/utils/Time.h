//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/port/Clocks.h"

#include <atomic>

namespace td {

class Time {
 public:
  static double now() {
    double now = Clocks::monotonic();
    now_.store(now, std::memory_order_relaxed);
    return now;
  }
  static double now_cached() {
    return now_.load(std::memory_order_relaxed);
  }

 private:
  static std::atomic<double> now_;
};

inline void relax_timeout_at(double *timeout, double new_timeout) {
  if (new_timeout == 0) {
    return;
  }
  if (*timeout == 0 || new_timeout < *timeout) {
    *timeout = new_timeout;
  }
}

class Timestamp {
 public:
  Timestamp() = default;
  static Timestamp never() {
    return Timestamp{};
  }
  static Timestamp now() {
    return Timestamp{Time::now()};
  }
  static Timestamp now_cached() {
    return Timestamp{Time::now_cached()};
  }
  static Timestamp at(double timeout) {
    return Timestamp{timeout};
  }

  static Timestamp in(double timeout) {
    return Timestamp{Time::now_cached() + timeout};
  }

  bool is_in_past() const {
    return at_ <= Time::now_cached();
  }

  explicit operator bool() const {
    return at_ > 0;
  }

  double at() const {
    return at_;
  }

  double in() const {
    return at_ - Time::now_cached();
  }

  void relax(const Timestamp &timeout) {
    if (!timeout) {
      return;
    }
    if (!*this || at_ > timeout.at_) {
      at_ = timeout.at_;
    }
  }

  friend bool operator==(Timestamp a, Timestamp b);

 private:
  double at_{0};

  explicit Timestamp(double timeout) : at_(timeout) {
  }
};

template <class T>
void parse(Timestamp &timestamp, T &parser) {
  timestamp = Timestamp::in(parser.fetch_double() - Clocks::system());
}

template <class T>
void store(const Timestamp &timestamp, T &storer) {
  storer.store_binary(timestamp.at() - Time::now() + Clocks::system());
}

}  // namespace td
