//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/detail/ThreadPthread.h"

#if TD_THREAD_PTHREAD
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <sys/sysctl.h>

namespace td {
namespace detail {
unsigned ThreadPthread::hardware_concurrency() {
// Linux and MacOs
#if defined(_SC_NPROCESSORS_ONLN)
  {
    auto res = sysconf(_SC_NPROCESSORS_ONLN);
    if (res > 0) {
      return narrow_cast<unsigned>(res);
    }
  }
#endif

// *BSD
#if defined(HW_AVAILCPU) && defined(CTL_HW)
  {
    int mib[2];
    int res{0};
    size_t len = sizeof(res);
    mib[0] = CTL_HW;
    mib[1] = HW_AVAILCPU;
    len = sizeof(res);
    if (sysctl(mib, 2, &res, &len, NULL, 0) == 0 && res != 0) {
      return res;
    }
  }
#endif

#if defined(HW_NCPU) && defined(CTL_HW)
  {
    int mib[2];
    int res{0};
    size_t len = sizeof(res);
    mib[0] = CTL_HW;
    mib[1] = HW_NCPU;
    len = sizeof(res);
    if (sysctl(mib, 2, &res, &len, NULL, 0) == 0 && res != 0) {
      return res;
    }
  }
#endif

  // Just in case
  return 8;
}
void ThreadPthread::set_name(CSlice name) {
#if defined(_GNU_SOURCE) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2, 12)
  pthread_setname_np(thread_, name.c_str());
#endif
#endif
}
void ThreadPthread::join() {
  if (is_inited_.get()) {
    is_inited_ = false;
    pthread_join(thread_, nullptr);
  }
}
void ThreadPthread::detach() {
  if (is_inited_.get()) {
    is_inited_ = false;
    pthread_detach(thread_);
  }
}
int do_pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg) {
  return pthread_create(thread, attr, start_routine, arg);
}
namespace this_thread_pthread {
void yield() {
  sched_yield();
}
ThreadPthread::id get_id() {
  return pthread_self();
}
}  // namespace this_thread_pthread
}  // namespace detail
}  // namespace td
#endif
