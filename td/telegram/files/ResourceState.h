//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/StringBuilder.h"

namespace td {

class ResourceState {
 public:
  void start_use(int64 x) {
    using_ += x;
    CHECK(used_ + using_ <= limit_);
  }

  void stop_use(int64 x) {
    CHECK(x <= using_);
    using_ -= x;
    used_ += x;
  }

  void update_limit(int64 extra) {
    limit_ += extra;
  }

  bool update_estimated_limit(int64 extra) {
    // unused() must be positive, i.e. used_ + using_ must be less than limit_
    // TODO: use exact intersection between using_ and extra.
    auto using_and_extra_intersection = min(using_, extra);  // between 0 and min(using_, extra)
    auto new_estimated_limit = used_ + using_ + extra - using_and_extra_intersection;

    // Use extra extra limit
    if (new_estimated_limit < limit_) {
      auto extra_limit = limit_ - new_estimated_limit;
      used_ += extra_limit;
      new_estimated_limit += extra_limit;
    }

    if (new_estimated_limit == estimated_limit_) {
      return false;
    }
    estimated_limit_ = new_estimated_limit;
    return true;
  }

  void set_unit_size(size_t new_unit_size) {
    unit_size_ = new_unit_size;
  }

  int64 active_limit() const {
    return limit_ - used_;
  }

  int64 get_using() const {
    return using_;
  }

  int64 unused() const {
    return limit_ - using_ - used_;
  }

  int64 estimated_extra() const {
    auto new_unused = max(limit_, estimated_limit_) - using_ - used_;
    new_unused = static_cast<int64>((new_unused + unit_size() - 1) / unit_size() * unit_size());
    return new_unused + using_ + used_ - limit_;
  }

  size_t unit_size() const {
    return unit_size_;
  }

  ResourceState &operator+=(const ResourceState &other) {
    using_ += other.active_limit();
    used_ += other.used_;
    return *this;
  }

  ResourceState &operator-=(const ResourceState &other) {
    using_ -= other.active_limit();
    used_ -= other.used_;
    return *this;
  }

  void update_master(const ResourceState &other) {
    estimated_limit_ = other.estimated_limit_;
    used_ = other.used_;
    using_ = other.using_;
    unit_size_ = other.unit_size_;
  }

  void update_slave(const ResourceState &other) {
    limit_ = other.limit_;
  }

  friend StringBuilder &operator<<(StringBuilder &sb, const ResourceState &state) {
    return sb << tag("estimated_limit", state.estimated_limit_) << tag("used", state.used_)
              << tag("using", state.using_) << tag("limit", state.limit_);
  }

 private:
  int64 estimated_limit_ = 0;  // me
  int64 limit_ = 0;            // master
  int64 used_ = 0;             // me
  int64 using_ = 0;            // me
  size_t unit_size_ = 1;       // me
};

}  // namespace td
