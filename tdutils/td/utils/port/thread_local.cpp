//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/thread_local.h"

namespace td {

namespace detail {

class ThreadLocalDestructorGuard final {
 public:
  ~ThreadLocalDestructorGuard();
};

ThreadLocalDestructorGuard &get_thread_local_destructor_guard() {
  static thread_local ThreadLocalDestructorGuard guard;
  return guard;
}

static TD_THREAD_LOCAL int32 thread_id_;
static TD_THREAD_LOCAL std::vector<unique_ptr<Destructor>> *thread_local_destructors;

void add_thread_local_destructor(unique_ptr<Destructor> destructor) {
  auto &guard = get_thread_local_destructor_guard();
  (void)guard;
  if (thread_local_destructors == nullptr) {
    auto destructors = std::make_unique<std::vector<unique_ptr<Destructor>>>();
    thread_local_destructors = destructors.release();
  }
  thread_local_destructors->push_back(std::move(destructor));
}

ThreadLocalDestructorGuard::~ThreadLocalDestructorGuard() {
  td::clear_thread_locals();
}

}  // namespace detail

void clear_thread_locals() {
  // ensure that no destructors were added during destructors invocation
  std::unique_ptr<std::vector<unique_ptr<Destructor>>> to_delete(detail::thread_local_destructors);
  detail::thread_local_destructors = nullptr;
  to_delete.reset();
  // V547: defense-in-depth — verifies destructors didn't re-register themselves
  CHECK(detail::thread_local_destructors == nullptr);
}

void set_thread_id(int32 id) {
  detail::thread_id_ = id;
}

int32 get_thread_id() {
  return detail::thread_id_;
}

}  // namespace td
