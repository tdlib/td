//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/detail/Poll.h"

char disable_linker_warning_about_empty_file_poll_cpp TD_UNUSED;

#ifdef TD_POLL_POLL

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Status.h"

namespace td {
namespace detail {

void Poll::init() {
}

void Poll::clear() {
  pollfds_.clear();
}

void Poll::subscribe(const Fd &fd, Fd::Flags flags) {
  unsubscribe(fd);
  struct pollfd pollfd;
  pollfd.fd = fd.get_native_fd();
  pollfd.events = 0;
  if (flags & Fd::Read) {
    pollfd.events |= POLLIN;
  }
  if (flags & Fd::Write) {
    pollfd.events |= POLLOUT;
  }
  pollfd.revents = 0;
  pollfds_.push_back(pollfd);
}

void Poll::unsubscribe(const Fd &fd) {
  for (auto it = pollfds_.begin(); it != pollfds_.end(); ++it) {
    if (it->fd == fd.get_native_fd()) {
      pollfds_.erase(it);
      return;
    }
  }
}

void Poll::unsubscribe_before_close(const Fd &fd) {
  unsubscribe(fd);
}

void Poll::run(int timeout_ms) {
  int err = poll(pollfds_.data(), narrow_cast<int>(pollfds_.size()), timeout_ms);
  auto poll_errno = errno;
  LOG_IF(FATAL, err == -1 && poll_errno != EINTR) << Status::PosixError(poll_errno, "poll failed");

  for (auto &pollfd : pollfds_) {
    Fd::Flags flags = 0;
    if (pollfd.revents & POLLIN) {
      pollfd.revents &= ~POLLIN;
      flags |= Fd::Read;
    }
    if (pollfd.revents & POLLOUT) {
      pollfd.revents &= ~POLLOUT;
      flags |= Fd::Write;
    }
    if (pollfd.revents & POLLHUP) {
      pollfd.revents &= ~POLLHUP;
      flags |= Fd::Close;
    }
    if (pollfd.revents & POLLERR) {
      pollfd.revents &= ~POLLERR;
      flags |= Fd::Error;
    }
    if (pollfd.revents & POLLNVAL) {
      LOG(FATAL) << "Unexpected POLLNVAL " << tag("fd", pollfd.fd);
    }
    if (pollfd.revents) {
      LOG(FATAL) << "Unsupported poll events: " << pollfd.revents;
    }
    Fd(pollfd.fd, Fd::Mode::Reference).update_flags_notify(flags);
  }
}

}  // namespace detail
}  // namespace td

#endif
