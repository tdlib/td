// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/net/SslCtx.h"

#include "td/utils/tests.h"

#include <atomic>
#include <thread>
#include <vector>

#if !TD_EMSCRIPTEN

TEST(SslCtxStress, ConcurrentUnverifiedCreateDestroyStaysStableAcrossFourteenWorkers) {
  constexpr td::int32 kThreadCount = 14;
  constexpr td::int32 kIterationsPerThread = 160;

  std::atomic<td::int32> failures{0};
  std::atomic<td::int32> null_context_successes{0};
  std::vector<std::thread> workers;
  workers.reserve(static_cast<std::size_t>(kThreadCount));

  for (td::int32 i = 0; i < kThreadCount; ++i) {
    workers.emplace_back([&] {
      for (td::int32 j = 0; j < kIterationsPerThread; ++j) {
        auto result = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off);
        if (result.is_error()) {
          failures.fetch_add(1, std::memory_order_relaxed);
          continue;
        }
        if (result.ok().get_openssl_ctx() == nullptr) {
          null_context_successes.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }

  for (auto &worker : workers) {
    worker.join();
  }

  ASSERT_EQ(failures.load(), 0);
  ASSERT_EQ(null_context_successes.load(), 0);
}

TEST(SslCtxStress, RepeatedMoveAssignmentPreservesUsableContextIdentity) {
  auto initial_result = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off);
  ASSERT_TRUE(initial_result.is_ok());

  auto current = initial_result.move_as_ok();
  void *expected = current.get_openssl_ctx();
  ASSERT_TRUE(expected != nullptr);

  for (td::int32 i = 0; i < 2000; ++i) {
    td::SslCtx next = std::move(current);
    ASSERT_TRUE(next.get_openssl_ctx() == expected);
    current = std::move(next);
    ASSERT_TRUE(current.get_openssl_ctx() == expected);
  }
}

TEST(SslCtxStress, RepeatedMissingExplicitBundleFailureRemainsDeterministic) {
  constexpr td::CSlice kPath = "/tmp/sslctx-stress-no-such-bundle.pem";
  const bool first_on = td::SslCtx::create(kPath, td::SslCtx::VerifyPeer::On).is_ok();
  const bool first_off = td::SslCtx::create(kPath, td::SslCtx::VerifyPeer::Off).is_ok();

  for (td::int32 i = 0; i < 1000; ++i) {
    ASSERT_EQ(td::SslCtx::create(kPath, td::SslCtx::VerifyPeer::On).is_ok(), first_on);
    ASSERT_EQ(td::SslCtx::create(kPath, td::SslCtx::VerifyPeer::Off).is_ok(), first_off);
  }
}

#endif
