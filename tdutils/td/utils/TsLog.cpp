// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
#include "td/utils/TsLog.h"

#include "td/utils/ExitGuard.h"

#include <thread>

namespace td {

bool TsLog::enter_critical() {
  uint32 spin_count = 0;
  while (lock_.test_and_set(std::memory_order_acquire)) {
    if (ExitGuard::is_exited()) {
      return false;
    }
    spin_count++;
    if ((spin_count & 31u) == 0) {
      std::this_thread::yield();
    }
  }
  return true;
}

static ExitGuard exit_guard;

}  // namespace td
