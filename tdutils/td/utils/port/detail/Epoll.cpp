//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/detail/Epoll.h"

char disable_linker_warning_about_empty_file_epoll_cpp TD_UNUSED;

#ifdef TD_POLL_EPOLL

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"

#include <unistd.h>

namespace td {
namespace detail {
void Epoll::init() {
  CHECK(epoll_fd == -1);
  epoll_fd = epoll_create(1);
  auto epoll_create_errno = errno;
  LOG_IF(FATAL, epoll_fd == -1) << Status::PosixError(epoll_create_errno, "epoll_create failed");

  events.resize(1000);
}

void Epoll::clear() {
  if (epoll_fd == -1) {
    return;
  }
  events.clear();

  close(epoll_fd);
  epoll_fd = -1;
}

void Epoll::subscribe(const Fd &fd, Fd::Flags flags) {
  epoll_event event;
  event.events = EPOLLHUP | EPOLLERR | EPOLLET;
#ifdef EPOLLRDHUP
  event.events |= EPOLLRDHUP;
#endif
  if (flags & Fd::Read) {
    event.events |= EPOLLIN;
  }
  if (flags & Fd::Write) {
    event.events |= EPOLLOUT;
  }
  auto native_fd = fd.get_native_fd();
  event.data.fd = native_fd;
  int err = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, native_fd, &event);
  auto epoll_ctl_errno = errno;
  LOG_IF(FATAL, err == -1) << Status::PosixError(epoll_ctl_errno, "epoll_ctl ADD failed") << ", epoll_fd = " << epoll_fd
                           << ", fd = " << native_fd;
}

void Epoll::unsubscribe(const Fd &fd) {
  auto native_fd = fd.get_native_fd();
  int err = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, native_fd, nullptr);
  auto epoll_ctl_errno = errno;
  LOG_IF(FATAL, err == -1) << Status::PosixError(epoll_ctl_errno, "epoll_ctl DEL failed") << ", epoll_fd = " << epoll_fd
                           << ", fd = " << native_fd;
}

void Epoll::unsubscribe_before_close(const Fd &fd) {
  unsubscribe(fd);
}

void Epoll::run(int timeout_ms) {
  int ready_n = epoll_wait(epoll_fd, &events[0], static_cast<int>(events.size()), timeout_ms);
  auto epoll_wait_errno = errno;
  LOG_IF(FATAL, ready_n == -1 && epoll_wait_errno != EINTR)
      << Status::PosixError(epoll_wait_errno, "epoll_wait failed");

  for (int i = 0; i < ready_n; i++) {
    Fd::Flags flags = 0;
    epoll_event *event = &events[i];
    if (event->events & EPOLLIN) {
      event->events &= ~EPOLLIN;
      flags |= Fd::Read;
    }
    if (event->events & EPOLLOUT) {
      event->events &= ~EPOLLOUT;
      flags |= Fd::Write;
    }
#ifdef EPOLLRDHUP
    if (event->events & EPOLLRDHUP) {
      event->events &= ~EPOLLRDHUP;
      //      flags |= Fd::Close;
      // TODO
    }
#endif
    if (event->events & EPOLLHUP) {
      event->events &= ~EPOLLHUP;
      flags |= Fd::Close;
    }
    if (event->events & EPOLLERR) {
      event->events &= ~EPOLLERR;
      flags |= Fd::Error;
    }
    if (event->events) {
      LOG(FATAL) << "Unsupported epoll events: " << event->events;
    }
    // LOG(DEBUG) << "Epoll event " << tag("fd", event->data.fd) << tag("flags", format::as_binary(flags));
    Fd(event->data.fd, Fd::Mode::Reference).update_flags_notify(flags);
  }
}
}  // namespace detail
}  // namespace td

#endif
