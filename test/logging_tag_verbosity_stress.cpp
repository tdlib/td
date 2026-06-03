// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/telegram/Logging.h"

#include "td/utils/tests.h"

#include <atomic>
#include <thread>

namespace {

using td::logging_hardening::test::load_repo_text;

TEST(LoggingTagVerbosityStress, ConcurrentTagUpdatesRemainClampedAndReadableOn14Threads) {
  constexpr int kThreads = 14;
  constexpr int kPerThread = 6000;

  auto before = td::Logging::get_tag_verbosity_level("td_init");
  ASSERT_TRUE(before.is_ok());

  std::atomic<bool> all_ok{true};
  td::vector<std::thread> workers;
  workers.reserve(kThreads);

  for (int t = 0; t < kThreads; t++) {
    workers.emplace_back([t, &all_ok] {
      for (int i = 0; i < kPerThread; i++) {
        const int level = ((t + 1) * (i + 3)) % (VERBOSITY_NAME(NEVER) + 800) - 200;
        auto set_status = td::Logging::set_tag_verbosity_level("td_init", level);
        if (set_status.is_error()) {
          all_ok.store(false, std::memory_order_relaxed);
          return;
        }

        auto get_result = td::Logging::get_tag_verbosity_level("td_init");
        if (get_result.is_error()) {
          all_ok.store(false, std::memory_order_relaxed);
          return;
        }
        if (get_result.ok() < 1 || get_result.ok() > VERBOSITY_NAME(NEVER)) {
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
  ASSERT_TRUE(td::Logging::set_tag_verbosity_level("td_init", before.ok()).is_ok());
}

TEST(LoggingTagVerbosityStress, SourceRequiresAtomicTagDeclarationsInAllKnownDefinitions) {
  auto td_cpp = load_repo_text("td/telegram/Td.cpp");
  auto file_manager_cpp = load_repo_text("td/telegram/files/FileManager.cpp");
  auto logging_cpp = load_repo_text("td/telegram/Logging.cpp");

  ASSERT_TRUE(td_cpp.find("std::atomic<int> VERBOSITY_NAME(td_init)") != td::string::npos);
  ASSERT_TRUE(td_cpp.find("std::atomic<int> VERBOSITY_NAME(td_requests)") != td::string::npos);
  ASSERT_TRUE(file_manager_cpp.find("std::atomic<int> VERBOSITY_NAME(update_file)") != td::string::npos);
  ASSERT_TRUE(logging_cpp.find("struct LogTagEntry") != td::string::npos);
  ASSERT_TRUE(logging_cpp.find("std::map<Slice, std::atomic<int> *>") == td::string::npos);
}

}  // namespace
