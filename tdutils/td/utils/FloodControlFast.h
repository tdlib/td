//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/TimedStat.h"

namespace td {

class FloodControlFast {
 public:
  void add_event(int32 now) {
    for (auto &limit : limits_) {
      limit.stat_.add_event(CounterStat::Event(), now);
      if (limit.stat_.get_stat(now).count_ > limit.count_) {
        wakeup_at_ = max(wakeup_at_, now + limit.duration_ * 2);
      }
    }
  }

  uint32 get_wakeup_at() const {
    return wakeup_at_;
  }

  void add_limit(uint32 duration, int32 count) {
    limits_.push_back({TimedStat<CounterStat>(duration, 0), duration, count});
  }

  void clear_events() {
    for (auto &limit : limits_) {
      limit.stat_.clear_events();
    }
    wakeup_at_ = 0;
  }

 private:
  class CounterStat {
   public:
    struct Event {};
    int32 count_ = 0;
    void on_event(Event e) {
      count_++;
    }
    void clear() {
      count_ = 0;
    }
  };

  uint32 wakeup_at_ = 0;
  struct Limit {
    TimedStat<CounterStat> stat_;
    uint32 duration_;
    int32 count_;
  };
  std::vector<Limit> limits_;
};

}  // namespace td
