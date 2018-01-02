//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/detail/ThreadIdGuard.h"

#include "td/utils/logging.h"
#include "td/utils/port/thread_local.h"

#include <array>
#include <mutex>

namespace td {
namespace detail {
class ThreadIdManager {
 public:
  int32 register_thread() {
    std::lock_guard<std::mutex> guard(mutex_);
    for (size_t i = 0; i < is_id_used_.size(); i++) {
      if (!is_id_used_[i]) {
        is_id_used_[i] = true;
        return static_cast<int32>(i + 1);
      }
    }
    LOG(FATAL) << "Cannot create more than " << max_thread_count() << " threads";
    return 0;
  }
  void unregister_thread(int32 thread_id) {
    thread_id--;
    std::lock_guard<std::mutex> guard(mutex_);
    CHECK(is_id_used_.at(thread_id));
    is_id_used_[thread_id] = false;
  }

 private:
  std::mutex mutex_;
  std::array<bool, max_thread_count()> is_id_used_{{false}};
};
static ThreadIdManager thread_id_manager;

ThreadIdGuard::ThreadIdGuard() {
  thread_id_ = thread_id_manager.register_thread();
  set_thread_id(thread_id_);
}
ThreadIdGuard::~ThreadIdGuard() {
  thread_id_manager.unregister_thread(thread_id_);
  set_thread_id(0);
}
}  // namespace detail
}  // namespace td
