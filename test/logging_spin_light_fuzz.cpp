// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/utils/logging.h"
#include "td/utils/tests.h"
#include "td/utils/TsLog.h"

#include <atomic>
#include <thread>

namespace {

class FuzzCountingLog final : public td::LogInterface {
 public:
  explicit FuzzCountingLog(td::string path_name) : path_name_(std::move(path_name)) {
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

td::uint64 next_fuzz_value(td::uint64 &state) {
  state = state * 6364136223846793005ULL + 1442695040888963407ULL;
  return state;
}

TEST(LoggingSpinLightFuzz, TsLogRandomizedOperationMixPreservesAppendAccounting) {
  FuzzCountingLog sink_a("spin-fuzz-a.log");
  FuzzCountingLog sink_b("spin-fuzz-b.log");
  td::TsLog ts_log(&sink_a);

  constexpr int kThreads = 4;
  constexpr int kStepsPerThread = 10000;

  std::atomic<td::uint64> attempted_appends{0};
  std::atomic<td::uint64> empty_path_reads{0};

  td::vector<std::thread> workers;
  workers.reserve(kThreads);
  for (int t = 0; t < kThreads; t++) {
    workers.emplace_back([&, t] {
      td::uint64 state = 0x9e3779b97f4a7c15ULL ^ static_cast<td::uint64>(t + 1);
      constexpr td::CSlice kMessage("spin-light-fuzz");
      for (int i = 0; i < kStepsPerThread; i++) {
        auto fuzz_value = next_fuzz_value(state);
        switch (fuzz_value % 10u) {
          case 0:
          case 1:
          case 2:
          case 3:
          case 4:
          case 5:
            attempted_appends.fetch_add(1, std::memory_order_relaxed);
            ts_log.append(VERBOSITY_NAME(PLAIN), kMessage);
            break;
          case 6:
          case 7:
            ts_log.init((fuzz_value & 1u) == 0u ? static_cast<td::LogInterface *>(&sink_a)
                                                : static_cast<td::LogInterface *>(&sink_b));
            break;
          case 8:
            ts_log.after_rotation();
            break;
          default: {
            auto paths = ts_log.get_file_paths();
            if (paths.empty()) {
              empty_path_reads.fetch_add(1, std::memory_order_relaxed);
            }
            break;
          }
        }
      }
    });
  }

  for (auto &worker : workers) {
    worker.join();
  }

  const auto observed_appends =
      sink_a.writes.load(std::memory_order_relaxed) + sink_b.writes.load(std::memory_order_relaxed);
  const auto observed_rotations =
      sink_a.rotations.load(std::memory_order_relaxed) + sink_b.rotations.load(std::memory_order_relaxed);

  ASSERT_EQ(attempted_appends.load(std::memory_order_relaxed), observed_appends);
  ASSERT_TRUE(observed_rotations > 0u);
  ASSERT_EQ(0u, empty_path_reads.load(std::memory_order_relaxed));
}

}  // namespace