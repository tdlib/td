//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/port/config.h"

#ifdef TD_POLL_KQUEUE

#include "td/utils/common.h"
#include "td/utils/port/Fd.h"
#include "td/utils/port/PollBase.h"

#include <cstdint>

#include <sys/event.h>

namespace td {
namespace detail {

class KQueue final : public PollBase {
 public:
  KQueue();
  KQueue(const KQueue &) = delete;
  KQueue &operator=(const KQueue &) = delete;
  KQueue(KQueue &&) = delete;
  KQueue &operator=(KQueue &&) = delete;
  ~KQueue() override;

  void init() override;

  void clear() override;

  void subscribe(const Fd &fd, Fd::Flags flags) override;

  void unsubscribe(const Fd &fd) override;

  void unsubscribe_before_close(const Fd &fd) override;

  void run(int timeout_ms) override;

 private:
  vector<struct kevent> events;
  int changes_n;
  int kq;

  int update(int nevents, const timespec *timeout, bool may_fail = false);

  void invalidate(const Fd &fd);

  void flush_changes(bool may_fail = false);

  void add_change(std::uintptr_t ident, int16 filter, uint16 flags, uint32 fflags, std::intptr_t data, void *udata);
};

}  // namespace detail
}  // namespace td

#endif
