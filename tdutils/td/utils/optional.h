//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/Status.h"

#include <utility>

namespace td {

template <class T>
class optional {
 public:
  optional() = default;
  template <class T1>
  optional(T1 &&t) : impl_(std::forward<T1>(t)) {
  }
  explicit operator bool() {
    return impl_.is_ok();
  }
  T &value() {
    return impl_.ok_ref();
  }
  T &operator*() {
    return value();
  }

 private:
  Result<T> impl_;
};

}  // namespace td
