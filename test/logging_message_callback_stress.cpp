// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/utils/logging.h"
#include "td/utils/tests.h"

#include <atomic>
#include <thread>

namespace {

class DiscardLog final : public td::LogInterface {
 public:
  void do_append(int log_level, td::CSlice slice) final {
    (void)log_level;
    (void)slice;
  }
};

std::atomic<int> g_stress_calls{0};

void stress_callback(int verbosity_level, td::CSlice message) {
  (void)verbosity_level;
  (void)message;
  g_stress_calls.fetch_add(1, std::memory_order_relaxed);
}

TEST(LoggingMessageCallbackStress, ConcurrentEligibleLogsInvokeCallbackForEveryEmissionOn14Threads) {
  DiscardLog sink;
  td::LogOptions options(VERBOSITY_NAME(DEBUG), false, false);

  constexpr int kThreads = 14;
  constexpr int kPerThread = 1200;

  g_stress_calls.store(0, std::memory_order_relaxed);
  td::set_log_message_callback(VERBOSITY_NAME(INFO), &stress_callback);

  td::vector<std::thread> workers;
  workers.reserve(kThreads);
  for (int thread_id = 0; thread_id < kThreads; thread_id++) {
    workers.emplace_back([&sink, &options, thread_id] {
      for (int i = 0; i < kPerThread; i++) {
        td::Logger logger(sink, options, VERBOSITY_NAME(ERROR));
        logger << "stress-callback|" << thread_id << '|' << i;
      }
    });
  }

  for (auto &worker : workers) {
    worker.join();
  }

  td::set_log_message_callback(VERBOSITY_NAME(FATAL), nullptr);

  const auto expected = kThreads * kPerThread;
  ASSERT_EQ(expected, g_stress_calls.load(std::memory_order_relaxed));
}

}  // namespace