// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/crypto.h"
#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

#if TD_HAVE_OPENSSL
#include <openssl/rand.h>
#endif

#if TD_HAVE_OPENSSL

TEST(RandomOpenSslInitIntegration, explicit_crypto_init_keeps_secure_bytes_available) {
  td::init_openssl_threads();
  td::init_crypto();

  std::array<unsigned char, 64> first{};
  std::array<unsigned char, 64> second{};

  td::Random::secure_cleanup();
  td::Random::secure_bytes(first.data(), first.size());
  td::Random::secure_cleanup();
  td::Random::secure_bytes(second.data(), second.size());

  ASSERT_TRUE(std::any_of(first.begin(), first.end(), [](unsigned char byte) { return byte != 0; }));
  ASSERT_TRUE(std::any_of(second.begin(), second.end(), [](unsigned char byte) { return byte != 0; }));
  ASSERT_NE(0, std::memcmp(first.data(), second.data(), first.size()));
}

TEST(RandomOpenSslInitIntegration, stress_concurrent_init_crypto_and_secure_bytes) {
  static constexpr int kNumThreads = 16;
  static constexpr int kIterationsPerThread = 1000;
  static constexpr std::size_t kBufSize = 64;

  std::atomic<int> ready_count{0};
  std::atomic<bool> go{false};
  std::atomic<int> failure_count{0};

  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);

  for (int t = 0; t < kNumThreads; t++) {
    threads.emplace_back([&ready_count, &go, &failure_count] {
      // Barrier: signal readiness, then spin until all threads are released.
      ready_count.fetch_add(1, std::memory_order_release);
      while (!go.load(std::memory_order_acquire)) {
        // spin
      }

      // Every thread independently calls init_crypto(), exactly mirroring the
      // real startup path where multiple subsystems may race to initialize.
      td::init_crypto();

      for (int i = 0; i < kIterationsPerThread; i++) {
        std::array<unsigned char, kBufSize> buf{};
        td::Random::secure_bytes(buf.data(), buf.size());

        // Verify the buffer is not all-zero.  For 64 random bytes the
        // probability of a legitimate all-zero buffer is 2^{-512}, so a
        // failure here reliably indicates a broken RNG path.
        const bool all_zero = std::none_of(buf.begin(), buf.end(), [](unsigned char b) { return b != 0; });
        if (all_zero) {
          failure_count.fetch_add(1, std::memory_order_relaxed);
        }
      }

      // After the tight loop, RAND_status() must report that the PRNG is
      // properly seeded on this thread.
      if (RAND_status() != 1) {
        failure_count.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  // Wait until every thread has reached the barrier, then release them all
  // at once to maximise contention on init_crypto().
  while (ready_count.load(std::memory_order_acquire) < kNumThreads) {
    // spin
  }
  go.store(true, std::memory_order_release);

  for (auto &th : threads) {
    th.join();
  }

  ASSERT_EQ(0, failure_count.load(std::memory_order_relaxed));
}

#endif
