// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/utils/logging.h"
#include "td/utils/tests.h"
#include "td/utils/TsLog.h"

#include <atomic>
#include <thread>

namespace {

class SwitchingCountingLog final : public td::LogInterface {
 public:
  explicit SwitchingCountingLog(td::string path_name) : path_name_(std::move(path_name)) {
  }

  void do_append(int log_level, td::CSlice slice) final {
    (void)log_level;
    (void)slice;
    writes.fetch_add(1, std::memory_order_relaxed);
  }

  void after_rotation() final {
    rotations.fetch_add(1, std::memory_order_relaxed);
  }

  td::vector<td::string> get_file_paths() final {
    return {path_name_};
  }

  std::atomic<td::uint64> writes{0};
  std::atomic<td::uint64> rotations{0};

 private:
  td::string path_name_;
};

TEST(LoggingSpinIntegration, TsLogConcurrentInitAndAppendPreservesAppendAccounting) {
  SwitchingCountingLog sink_a("spin-integration-a.log");
  SwitchingCountingLog sink_b("spin-integration-b.log");
  td::TsLog ts_log(&sink_a);

  constexpr int kAppenderThreads = 8;
  constexpr int kPerThreadAppends = 7000;
  constexpr int kSwapIterations = 20000;

  std::atomic<bool> keep_swapping{true};
  std::atomic<td::uint64> empty_path_reads{0};

  std::thread swapper([&] {
    for (int i = 0; i < kSwapIterations; i++) {
      auto *target = (i & 1) == 0 ? static_cast<td::LogInterface *>(&sink_a) : static_cast<td::LogInterface *>(&sink_b);
      ts_log.init(target);
      if ((i & 63) == 0) {
        ts_log.after_rotation();
      }
    }
    keep_swapping.store(false, std::memory_order_relaxed);
  });

  td::vector<std::thread> workers;
  workers.reserve(kAppenderThreads);
  for (int t = 0; t < kAppenderThreads; t++) {
    workers.emplace_back([&] {
      constexpr td::CSlice kMessage("spin-integration");
      for (int i = 0; i < kPerThreadAppends; i++) {
        ts_log.append(VERBOSITY_NAME(PLAIN), kMessage);
        if ((i & 255) == 0 && keep_swapping.load(std::memory_order_relaxed)) {
          auto paths = ts_log.get_file_paths();
          if (paths.empty()) {
            empty_path_reads.fetch_add(1, std::memory_order_relaxed);
          }
        }
      }
    });
  }

  for (auto &worker : workers) {
    worker.join();
  }
  swapper.join();

  const auto total_writes =
      sink_a.writes.load(std::memory_order_relaxed) + sink_b.writes.load(std::memory_order_relaxed);
  const auto total_rotations =
      sink_a.rotations.load(std::memory_order_relaxed) + sink_b.rotations.load(std::memory_order_relaxed);
  const auto expected_writes = static_cast<td::uint64>(kAppenderThreads) * static_cast<td::uint64>(kPerThreadAppends);

  ASSERT_EQ(expected_writes, total_writes);
  ASSERT_TRUE(total_rotations > 0u);
  ASSERT_EQ(0u, empty_path_reads.load(std::memory_order_relaxed));
}

}  // namespace