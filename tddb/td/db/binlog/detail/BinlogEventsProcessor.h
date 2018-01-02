//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once
#include "td/db/binlog/BinlogEvent.h"

#include "td/utils/common.h"

namespace td {
namespace detail {
class BinlogEventsProcessor {
 public:
  void add_event(BinlogEvent &&event) {
    do_event(std::move(event));
  }

  template <class CallbackT>
  void for_each(CallbackT &&callback) {
    for (size_t i = 0; i < ids_.size(); i++) {
      if ((ids_[i] & 1) == 0) {
        callback(events_[i]);
      }
    }
  }

  uint64 last_id() const {
    return last_id_;
  }
  int64 offset() const {
    return offset_;
  }
  int64 total_raw_events_size() const {
    return total_raw_events_size_;
  }

 private:
  // holds (id * 2 + was_deleted)
  std::vector<uint64> ids_;
  std::vector<BinlogEvent> events_;
  size_t total_events_{0};
  size_t empty_events_{0};
  uint64 last_id_{0};
  int64 offset_{0};
  int64 total_raw_events_size_{0};

  void do_event(BinlogEvent &&event);
  void compactify();
};
}  // namespace detail
}  // namespace td
