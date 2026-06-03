// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
#include "td/utils/common.h"
#include "td/utils/EpochBasedMemoryReclamation.h"
#include "td/utils/logging.h"
#include "td/utils/port/thread.h"
#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include <atomic>

#if !TD_THREAD_UNSUPPORTED
TEST(EpochBaseMemoryReclamation, stress) {
  struct Node {
    std::atomic<std::string *> name_{nullptr};
    char pad[64];
  };

  int threads_n = 10;
  std::vector<Node> nodes(threads_n);
  td::EpochBasedMemoryReclamation<std::string> ebmr(threads_n + 1);
  auto locker = ebmr.get_locker(threads_n);
  locker.lock();
  locker.unlock();
  std::vector<td::thread> threads(threads_n);
  int thread_id = 0;
  for (auto &thread : threads) {
    thread = td::thread([&, thread_id] {
      auto locker = ebmr.get_locker(thread_id);
      locker.lock();
      for (int i = 0; i < 1000000; i++) {
        auto &node = nodes[td::Random::fast(0, threads_n - 1)];
        auto *str = node.name_.load(std::memory_order_acquire);
        if (str) {
          CHECK(*str == "one" || *str == "twotwo");
        }
        if ((i + 1) % 100 == 0) {
          locker.retire();
        }
        if (td::Random::fast(0, 5) == 0) {
          auto *new_str = new td::string(td::Random::fast_bool() ? "one" : "twotwo");
          if (node.name_.compare_exchange_strong(str, new_str, std::memory_order_acq_rel)) {
            locker.retire(str);
          } else {
            delete new_str;
          }
        }
      }
      locker.retire_sync();
      locker.unlock();
    });
    thread_id++;
  }
  for (auto &thread : threads) {
    thread.join();
  }

  // Retire the last values still referenced by nodes before final sync.
  auto cleanup_locker = ebmr.get_locker(threads_n);
  cleanup_locker.lock();
  for (auto &node : nodes) {
    auto *str = node.name_.exchange(nullptr, std::memory_order_acq_rel);
    if (str != nullptr) {
      cleanup_locker.retire(str);
    }
  }
  cleanup_locker.retire_sync();
  cleanup_locker.unlock();

  LOG(INFO) << "Undeleted pointers: " << ebmr.to_delete_size_unsafe();
  //CHECK(static_cast<int>(ebmr.to_delete_size_unsafe()) <= threads_n * threads_n);
  for (int i = 0; i < threads_n; i++) {
    ebmr.get_locker(i).retire_sync();
  }
  CHECK(ebmr.to_delete_size_unsafe() == 0);
}
#endif
