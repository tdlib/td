// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/utils/logging.h"
#include "td/utils/tests.h"
#include "td/utils/TsLog.h"

#include <atomic>
#include <thread>

namespace {

using td::logging_hardening::test::contains_any;
using td::logging_hardening::test::load_repo_text;

class CountingLog final : public td::LogInterface {
 public:
  void do_append(int log_level, td::CSlice slice) final {
    (void)log_level;
    (void)slice;
    writes.fetch_add(1, std::memory_order_relaxed);
  }

  std::atomic<td::uint64> writes{0};
};

TEST(LoggingSpinStress, TsLogContentionStressCompletesOn14Threads) {
  CountingLog sink;
  td::TsLog ts_log(&sink);

  constexpr int kThreads = 14;
  constexpr int kPerThread = 12000;
  td::vector<std::thread> workers;
  workers.reserve(kThreads);

  for (int t = 0; t < kThreads; t++) {
    workers.emplace_back([&ts_log] {
      constexpr td::CSlice kMessage("spin-stress");
      for (int i = 0; i < kPerThread; i++) {
        ts_log.append(VERBOSITY_NAME(PLAIN), kMessage);
      }
    });
  }

  for (auto &worker : workers) {
    worker.join();
  }

  ASSERT_EQ(static_cast<td::uint64>(kThreads * kPerThread), sink.writes.load(std::memory_order_relaxed));
}

TEST(LoggingSpinStress, TsLogSpinSourceRequiresBackoffHint) {
  auto source = load_repo_text("tdutils/td/utils/TsLog.cpp");

  ASSERT_TRUE(contains_any(source, {"std::this_thread::yield", "sched_yield", "spin_backoff", "cpu_relax"}));
}

}  // namespace
