//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/detail/ThreadIdGuard.h"

#include "td/utils/ExitGuard.h"
#include "td/utils/port/thread_local.h"

#include <algorithm>
#include <mutex>
#include <vector>

namespace td::detail {
class ThreadIdManager {
 public:
  int32 register_thread() {
    std::scoped_lock guard(mutex_);
    if (unused_thread_ids_.empty()) {
      CHECK(max_thread_id_ < kMaxRegisteredThreadId);
      return ++max_thread_id_;
    }
    auto result = unused_thread_ids_.back();
    unused_thread_ids_.pop_back();
    return result;
  }
  void unregister_thread(int32 thread_id) {
    std::scoped_lock guard(mutex_);
    CHECK(0 < thread_id && thread_id <= max_thread_id_);
    auto it = std::find(unused_thread_ids_.begin(), unused_thread_ids_.end(), thread_id);
    CHECK(it == unused_thread_ids_.end());
    unused_thread_ids_.push_back(thread_id);
  }

 private:
  std::mutex mutex_;
  std::vector<int32> unused_thread_ids_;
  int32 max_thread_id_ = 0;
};

ThreadIdManager &get_thread_id_manager() {
  static auto *manager = new ThreadIdManager();
  return *manager;
}

static const ExitGuard exit_guard;

ThreadIdGuard::ThreadIdGuard() {
  thread_id_ = get_thread_id_manager().register_thread();
  set_thread_id(thread_id_);
}
ThreadIdGuard::~ThreadIdGuard() {
  if (!ExitGuard::is_exited()) {
    get_thread_id_manager().unregister_thread(thread_id_);
  }
  set_thread_id(0);
}
}  // namespace td::detail
