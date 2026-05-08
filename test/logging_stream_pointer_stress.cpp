// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/telegram/Logging.h"

#include "td/utils/logging.h"

#include "td/utils/tests.h"

#include <atomic>
#include <thread>

namespace {

using td::logging_hardening::test::contains_any;
using td::logging_hardening::test::contains_identifier_assignment;
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

TEST(LoggingStreamPointerStress, ConcurrentRuntimeStreamSwitchesStayRecognizedOn14Threads) {
  constexpr int kThreads = 14;
  constexpr int kPerThread = 3000;

  std::atomic<bool> all_ok{true};
  td::vector<std::thread> workers;
  workers.reserve(kThreads);

  for (int t = 0; t < kThreads; t++) {
    workers.emplace_back([t, &all_ok] {
      for (int i = 0; i < kPerThread; i++) {
        auto use_default = ((i + t) & 1) == 0;
        auto status = use_default
                          ? td::Logging::set_current_stream(td::td_api::make_object<td::td_api::logStreamDefault>())
                          : td::Logging::set_current_stream(td::td_api::make_object<td::td_api::logStreamEmpty>());
        if (status.is_error()) {
          all_ok.store(false, std::memory_order_relaxed);
          return;
        }

        auto current = td::Logging::get_current_stream();
        if (current.is_error()) {
          all_ok.store(false, std::memory_order_relaxed);
          return;
        }

        auto id = current.ok()->get_id();
        if (id != td::td_api::logStreamDefault::ID && id != td::td_api::logStreamEmpty::ID) {
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
}

TEST(LoggingStreamPointerStress, ConcurrentSinkSwapsWithLiveLogEmissionPreserveAllMessages) {
  CountingLog sink_a;
  CountingLog sink_b;

  auto *old_sink = td::load_active_log_interface();
  const auto old_level = SET_VERBOSITY_LEVEL(VERBOSITY_NAME(INFO));
  td::store_active_log_interface(&sink_a);

  constexpr int kSwapThreads = 4;
  constexpr int kEmitThreads = 10;
  constexpr int kPerThread = 4000;

  std::atomic<bool> keep_swapping{true};
  td::vector<std::thread> workers;
  workers.reserve(kSwapThreads + kEmitThreads);

  for (int t = 0; t < kSwapThreads; t++) {
    workers.emplace_back([t, &keep_swapping, &sink_a, &sink_b] {
      td::LogInterface *sinks[2] = {&sink_a, &sink_b};
      int index = t & 1;
      while (keep_swapping.load(std::memory_order_relaxed)) {
        td::store_active_log_interface(sinks[index]);
        index ^= 1;
      }
    });
  }

  for (int t = 0; t < kEmitThreads; t++) {
    workers.emplace_back([t] {
      for (int i = 0; i < kPerThread; i++) {
        LOG(INFO) << "stream-pointer-race" << t << '-' << i;
      }
    });
  }

  for (int i = kSwapThreads; i < static_cast<int>(workers.size()); i++) {
    workers[i].join();
  }
  keep_swapping.store(false, std::memory_order_relaxed);
  for (int i = 0; i < kSwapThreads; i++) {
    workers[i].join();
  }

  td::store_active_log_interface(old_sink);
  SET_VERBOSITY_LEVEL(old_level);

  ASSERT_EQ(static_cast<td::uint64>(kEmitThreads * kPerThread),
            sink_a.writes.load(std::memory_order_relaxed) + sink_b.writes.load(std::memory_order_relaxed));
}

TEST(LoggingStreamPointerStress, SourceRejectsLegacyRawMutationInKnownDriftFiles) {
  const td::vector<td::string> files = {
      "tdutils/test/log.cpp",
      "test/stealth/test_tls_init_log_contract.cpp",
      "test/stealth/test_stealth_params_loader_reload_log_contract.cpp",
      "test/stealth/test_stream_transport_activation_fail_closed.cpp",
      "test/stealth/test_raw_connection_error_contract.cpp",
  };

  for (const auto &path : files) {
    auto source = load_repo_text(path);
    ASSERT_TRUE(!contains_identifier_assignment(source, "log_interface"));
    ASSERT_TRUE(contains_any(source, {"load_active_log_interface", "store_active_log_interface"}));
  }
}

}  // namespace
