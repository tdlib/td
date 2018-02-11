//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/detail/KQueue.h"

char disable_linker_warning_about_empty_file_kqueue_cpp TD_UNUSED;

#ifdef TD_POLL_KQUEUE

#include "td/utils/logging.h"
#include "td/utils/Status.h"

#include <utility>

#include <unistd.h>

namespace td {
namespace detail {

KQueue::KQueue() {
  kq = -1;
}
KQueue::~KQueue() {
  clear();
}
void KQueue::init() {
  kq = kqueue();
  auto kqueue_errno = errno;
  LOG_IF(FATAL, kq == -1) << Status::PosixError(kqueue_errno, "kqueue creation failed");

  // TODO: const
  events.resize(1000);
  changes_n = 0;
}

void KQueue::clear() {
  if (kq == -1) {
    return;
  }
  events.clear();
  close(kq);
  kq = -1;
}

int KQueue::update(int nevents, const timespec *timeout, bool may_fail) {
  int err = kevent(kq, &events[0], changes_n, &events[0], nevents, timeout);
  auto kevent_errno = errno;

  bool is_fatal_error = [&] {
    if (err != -1) {
      return false;
    }
    if (may_fail) {
      return kevent_errno != ENOENT;
    }
    return kevent_errno != EINTR;
  }();
  LOG_IF(FATAL, is_fatal_error) << Status::PosixError(kevent_errno, "kevent failed");

  changes_n = 0;
  if (err < 0) {
    return 0;
  }
  return err;
}

void KQueue::flush_changes(bool may_fail) {
  if (!changes_n) {
    return;
  }
  int n = update(0, nullptr, may_fail);
  CHECK(n == 0);
}

void KQueue::add_change(std::uintptr_t ident, int16 filter, uint16 flags, uint32 fflags, std::intptr_t data,
                        void *udata) {
  if (changes_n == static_cast<int>(events.size())) {
    flush_changes();
  }
  EV_SET(&events[changes_n], ident, filter, flags, fflags, data, udata);
  VLOG(fd) << "Subscribe [fd:" << ident << "] [filter:" << filter << "] [udata: " << udata << "]";
  changes_n++;
}

void KQueue::subscribe(const Fd &fd, Fd::Flags flags) {
  if (flags & Fd::Read) {
    add_change(fd.get_native_fd(), EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, nullptr);
  }
  if (flags & Fd::Write) {
    add_change(fd.get_native_fd(), EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, nullptr);
  }
}

void KQueue::invalidate(const Fd &fd) {
  for (int i = 0; i < changes_n; i++) {
    if (events[i].ident == static_cast<std::uintptr_t>(fd.get_native_fd())) {
      changes_n--;
      std::swap(events[i], events[changes_n]);
      i--;
    }
  }
}

void KQueue::unsubscribe(const Fd &fd) {
  // invalidate(fd);
  flush_changes();
  add_change(fd.get_native_fd(), EVFILT_READ, EV_DELETE, 0, 0, nullptr);
  flush_changes(true);
  add_change(fd.get_native_fd(), EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
  flush_changes(true);
}

void KQueue::unsubscribe_before_close(const Fd &fd) {
  invalidate(fd);

  // just to avoid O(changes_n ^ 2)
  if (changes_n != 0) {
    flush_changes();
  }
}

void KQueue::run(int timeout_ms) {
  timespec timeout_data;
  timespec *timeout_ptr;
  if (timeout_ms == -1) {
    timeout_ptr = nullptr;
  } else {
    timeout_data.tv_sec = timeout_ms / 1000;
    timeout_data.tv_nsec = timeout_ms % 1000 * 1000000;
    timeout_ptr = &timeout_data;
  }

  int n = update(static_cast<int>(events.size()), timeout_ptr);
  for (int i = 0; i < n; i++) {
    struct kevent *event = &events[i];
    Fd::Flags flags = 0;
    if (event->filter == EVFILT_WRITE) {
      flags |= Fd::Write;
    }
    if (event->filter == EVFILT_READ) {
      flags |= Fd::Read;
    }
    if (event->flags & EV_EOF) {
      flags |= Fd::Close;
    }
    if (event->fflags & EV_ERROR) {
      LOG(FATAL) << "EV_ERROR in kqueue is not supported";
    }
    VLOG(fd) << "Event [fd:" << event->ident << "] [filter:" << event->filter << "] [udata: " << event->udata << "]";
    // LOG(WARNING) << "event->ident = " << event->ident << "event->filter = " << event->filter;
    Fd(static_cast<int>(event->ident), Fd::Mode::Reference).update_flags_notify(flags);
  }
}
}  // namespace detail
}  // namespace td

#endif
