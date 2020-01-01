//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/StackAllocator.h"

#include "td/utils/port/thread_local.h"

namespace td {

StackAllocator::Impl &StackAllocator::impl() {
  static TD_THREAD_LOCAL StackAllocator::Impl *impl;  // static zero-initialized
  init_thread_local<Impl>(impl);
  return *impl;
}

}  // namespace td
