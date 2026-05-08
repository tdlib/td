// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_macro_test_utils.h"

#include "td/utils/logging.h"
#include "td/utils/tests.h"

#include <atomic>
#include <thread>

namespace {

using td::logging_macro::test::CountingLog;
using td::logging_macro::test::ScopedLoggingOverride;

std::atomic<int> VERBOSITY_NAME(macro_stress_tag){VERBOSITY_NAME(INFO)};

TEST(LoggingMacroStress, ConcurrentGlobalAndTagGateChurnCompletesOn14Threads) {
  constexpr int kThreads = 14;
  constexpr int kPerThread = 4000;

  CountingLog sink;
  ScopedLoggingOverride guard(&sink, VERBOSITY_NAME(DEBUG));

  std::atomic<bool> all_ok{true};
  td::vector<std::thread> workers;
  workers.reserve(kThreads);

  for (int t = 0; t < kThreads; t++) {
    workers.emplace_back([t, &all_ok] {
      for (int i = 0; i < kPerThread; i++) {
        const int runtime_level = ((i + t) & 1) == 0 ? VERBOSITY_NAME(INFO) : VERBOSITY_NAME(WARNING);
        const int tag_level = ((i + t) % 3) == 0 ? VERBOSITY_NAME(ERROR) : VERBOSITY_NAME(DEBUG);

        SET_VERBOSITY_LEVEL(runtime_level);
        VERBOSITY_NAME(macro_stress_tag).store(tag_level, std::memory_order_release);

        VLOG(macro_stress_tag) << "logging-macro-stress" << t << '-' << i;

        const int current = GET_VERBOSITY_LEVEL();
        if (current < VERBOSITY_NAME(FATAL) || current > VERBOSITY_NAME(NEVER)) {
          all_ok.store(false, std::memory_order_relaxed);
          return;
        }
      }
    });
  }

  for (auto &worker : workers) {
    worker.join();
  }

  ASSERT_TRUE(all_ok.load(std::memory_order_relaxed));

  const int writes = sink.writes.load(std::memory_order_relaxed);
  ASSERT_TRUE(writes > 0);
  ASSERT_TRUE(writes <= kThreads * kPerThread);
}

}  // namespace
