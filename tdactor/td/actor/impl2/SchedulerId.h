//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/logging.h"

namespace td {
namespace actor2 {
class SchedulerId {
 public:
  SchedulerId() : id_(-1) {
  }
  explicit SchedulerId(uint8 id) : id_(id) {
  }
  bool is_valid() const {
    return id_ >= 0;
  }
  uint8 value() const {
    CHECK(is_valid());
    return static_cast<uint8>(id_);
  }

 private:
  int32 id_{0};
};
}  // namespace actor2
}  // namespace td
