// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/mtproto/BlobStore.h"

#include "td/utils/tests.h"

#include <atomic>
#include <thread>
#include <vector>

namespace {

TEST(BlobStoreStress, ConcurrentBundleChecksStayDeterministic) {
  constexpr td::uint32 thread_count = 4;
  constexpr td::uint32 iterations_per_thread = 32;
  std::atomic<bool> is_ok{true};

  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  for (td::uint32 index = 0; index < thread_count; index++) {
    threads.emplace_back([&] {
      for (td::uint32 iteration = 0; iteration < iterations_per_thread; iteration++) {
        if (td::mtproto::BlobStore::verify_bundle().is_error()) {
          is_ok.store(false, std::memory_order_relaxed);
          return;
        }
        auto rsa = td::mtproto::BlobStore::load(td::mtproto::BlobRole::Primary);
        if (rsa.is_error() ||
            rsa.ok().get_fingerprint() != td::mtproto::BlobStore::expected_slot(td::mtproto::BlobRole::Primary)) {
          is_ok.store(false, std::memory_order_relaxed);
          return;
        }
      }
    });
  }

  for (auto &thread : threads) {
    thread.join();
  }

  ASSERT_TRUE(is_ok.load(std::memory_order_relaxed));
}

}  // namespace