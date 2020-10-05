//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/db/binlog/detail/BinlogEventsProcessor.h"

#include "td/utils/logging.h"
#include "td/utils/Status.h"

#include <algorithm>

namespace td {
namespace detail {

Status BinlogEventsProcessor::do_event(BinlogEvent &&event) {
  offset_ = event.offset_;
  auto fixed_id = event.id_ * 2;
  if ((event.flags_ & BinlogEvent::Flags::Rewrite) && !ids_.empty() && ids_.back() >= fixed_id) {
    auto it = std::lower_bound(ids_.begin(), ids_.end(), fixed_id);
    if (it == ids_.end() || *it != fixed_id) {
      return Status::Error(PSLICE() << "Ignore rewrite log event " << event.public_to_string());
    }
    auto pos = it - ids_.begin();
    total_raw_events_size_ -= static_cast<int64>(events_[pos].raw_event_.size());
    if (event.type_ == BinlogEvent::ServiceTypes::Empty) {
      *it += 1;
      empty_events_++;
      events_[pos].clear();
    } else {
      event.flags_ &= ~BinlogEvent::Flags::Rewrite;
      total_raw_events_size_ += static_cast<int64>(event.raw_event_.size());
      events_[pos] = std::move(event);
    }
  } else if (event.type_ < 0) {
    // just skip service events
  } else {
    if (!(ids_.empty() || ids_.back() < fixed_id)) {
      return Status::Error(PSLICE() << offset_ << " " << ids_.size() << " " << ids_.back() << " " << fixed_id << " "
                                    << event.public_to_string() << " " << total_events_ << " "
                                    << total_raw_events_size_);
    }
    last_id_ = event.id_;
    total_raw_events_size_ += static_cast<int64>(event.raw_event_.size());
    total_events_++;
    ids_.push_back(fixed_id);
    events_.emplace_back(std::move(event));
  }

  if (total_events_ > 10 && empty_events_ * 4 > total_events_ * 3) {
    compactify();
  }
  return Status::OK();
}

void BinlogEventsProcessor::compactify() {
  CHECK(ids_.size() == events_.size());
  auto ids_from = ids_.begin();
  auto ids_to = ids_from;
  auto events_from = events_.begin();
  auto events_to = events_from;
  for (; ids_from != ids_.end(); ids_from++, events_from++) {
    if ((*ids_from & 1) == 0) {
      *ids_to++ = *ids_from;
      *events_to++ = std::move(*events_from);
    }
  }
  ids_.erase(ids_to, ids_.end());
  events_.erase(events_to, events_.end());
  total_events_ = ids_.size();
  empty_events_ = 0;
  CHECK(ids_.size() == events_.size());
}

}  // namespace detail
}  // namespace td
