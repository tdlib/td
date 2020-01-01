//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/detail/ThreadPthread.h"

char disable_linker_warning_about_empty_file_thread_pthread_cpp TD_UNUSED;

#if TD_THREAD_PTHREAD

#include "td/utils/misc.h"

#include <pthread.h>
#include <sched.h>
#if TD_FREEBSD || TD_OPENBSD || TD_NETBSD
#include <sys/sysctl.h>
#endif
#include <unistd.h>

namespace td {
namespace detail {
unsigned ThreadPthread::hardware_concurrency() {
// Linux and macOS
#if defined(_SC_NPROCESSORS_ONLN)
  {
    auto res = sysconf(_SC_NPROCESSORS_ONLN);
    if (res > 0) {
      return narrow_cast<unsigned>(res);
    }
  }
#endif

#if TD_FREEBSD || TD_OPENBSD || TD_NETBSD
#if defined(HW_AVAILCPU) && defined(CTL_HW)
  {
    int mib[2] = {CTL_HW, HW_AVAILCPU};
    int res{0};
    size_t len = sizeof(res);
    if (sysctl(mib, 2, &res, &len, nullptr, 0) == 0 && res != 0) {
      return res;
    }
  }
#endif

#if defined(HW_NCPU) && defined(CTL_HW)
  {
    int mib[2] = {CTL_HW, HW_NCPU};
    int res{0};
    size_t len = sizeof(res);
    if (sysctl(mib, 2, &res, &len, nullptr, 0) == 0 && res != 0) {
      return res;
    }
  }
#endif
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

int ThreadPthread::do_pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *),
                                     void *arg) {
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
