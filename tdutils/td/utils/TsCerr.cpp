// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
#include "td/utils/TsCerr.h"

#include "td/utils/ExitGuard.h"
#include "td/utils/port/StdStreams.h"
#include "td/utils/Time.h"

#include <cerrno>
#include <thread>

namespace td {

std::atomic_flag &TsCerr::lock() {
  static std::atomic_flag lock_instance = ATOMIC_FLAG_INIT;
  return lock_instance;
}

TsCerr::TsCerr() {
  lock_is_acquired_ = enterCritical();
}

TsCerr::~TsCerr() {
  if (lock_is_acquired_) {
    exitCritical();
  }
}

TsCerr &TsCerr::operator<<(Slice slice) {
  if (!lock_is_acquired_) {
    return *this;
  }

  auto &fd = Stderr();
  if (fd.empty()) {
    return *this;
  }
  double end_time = 0;
  bool should_stop = false;
  while (!slice.empty() && !should_stop) {
    auto res = fd.write(slice);
    if (res.is_error()) {
      if (res.error().code() == EPIPE) {
        should_stop = true;
      }
      // Resource temporary unavailable
      if (!should_stop && end_time == 0) {
        end_time = Time::now() + 0.01;
      } else if (!should_stop && Time::now() > end_time) {
        should_stop = true;
      }
      continue;
    }
    slice.remove_prefix(res.ok());
  }
  return *this;
}

bool TsCerr::enterCritical() {
  unsigned int spin_count = 0;
  while (lock().test_and_set(std::memory_order_acquire)) {
    if (ExitGuard::is_exited()) {
      return false;
    }
    spin_count++;
    if ((spin_count & 31u) == 0u) {
      std::this_thread::yield();
    }
  }
  return true;
}

void TsCerr::exitCritical() {
  lock().clear(std::memory_order_release);
}

static const ExitGuard exit_guard;

}  // namespace td
