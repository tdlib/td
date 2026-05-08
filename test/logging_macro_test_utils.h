// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#pragma once

#include "td/utils/logging.h"

#include <atomic>

namespace td {
namespace logging_macro {
namespace test {

class CountingLog final : public td::LogInterface {
 public:
  void do_append(int log_level, td::CSlice slice) final {
    (void)log_level;
    (void)slice;
    writes.fetch_add(1, std::memory_order_relaxed);
  }

  std::atomic<int> writes{0};
};

class ScopedLoggingOverride final {
 public:
  ScopedLoggingOverride(td::LogInterface *sink, int verbosity_level)
      : old_sink_(td::load_active_log_interface()), old_level_(SET_VERBOSITY_LEVEL(verbosity_level)) {
    td::store_active_log_interface(sink);
  }

  ~ScopedLoggingOverride() {
    td::store_active_log_interface(old_sink_);
    SET_VERBOSITY_LEVEL(old_level_);
  }

 private:
  td::LogInterface *old_sink_;
  int old_level_;
};

}  // namespace test
}  // namespace logging_macro
}  // namespace td
