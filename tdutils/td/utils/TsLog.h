// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/Slice.h"

#include <atomic>

namespace td {

class TsLog final : public LogInterface {
 public:
  explicit TsLog(LogInterface *log) : log_(log) {
  }
  void init(LogInterface *log) {
    if (!enter_critical()) {
      return;
    }
    log_ = log;
    exit_critical();
  }
  void after_rotation() final {
    if (!enter_critical()) {
      return;
    }
    log_->after_rotation();
    exit_critical();
  }
  vector<string> get_file_paths() final {
    if (!enter_critical()) {
      return {};
    }
    auto result = log_->get_file_paths();
    exit_critical();
    return result;
  }

 private:
  void do_append(int log_level, CSlice slice) final {
    if (!enter_critical()) {
      return;
    }
    log_->do_append(log_level, slice);
    exit_critical();
  }

  bool enter_critical();

  void exit_critical() {
    lock_.clear(std::memory_order_release);
  }

  LogInterface *log_ = nullptr;
  std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
};

}  // namespace td
