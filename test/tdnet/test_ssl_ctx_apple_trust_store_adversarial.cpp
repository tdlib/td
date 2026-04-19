// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#if TD_DARWIN && !TD_EMSCRIPTEN

#include "td/net/SslCtx.h"

#include "td/utils/tests.h"

#include <atomic>
#include <thread>
#include <vector>

TEST(SslCtxAppleTrustStoreAdversarial, InterleavedVerifyModesDoNotCrossContaminate) {
  for (td::int32 i = 0; i < 16; ++i) {
    auto on_result = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::On);
    auto off_result = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off);

    ASSERT_TRUE(off_result.is_ok());
    if (on_result.is_ok()) {
      ASSERT_TRUE(on_result.ok().get_openssl_ctx() != off_result.ok().get_openssl_ctx());
    }
  }
}

TEST(SslCtxAppleTrustStoreAdversarial, ConcurrentVerifyOffCreationRemainsStable) {
  constexpr td::int32 kThreadCount = 8;
  constexpr td::int32 kIterations = 64;

  std::atomic<td::int32> failures{0};
  std::vector<std::thread> workers;
  workers.reserve(kThreadCount);

  for (td::int32 i = 0; i < kThreadCount; ++i) {
    workers.emplace_back([&] {
      for (td::int32 j = 0; j < kIterations; ++j) {
        auto result = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off);
        if (!result.is_ok()) {
          failures.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }

  for (auto &worker : workers) {
    worker.join();
  }

  ASSERT_EQ(failures.load(), 0);
}

TEST(SslCtxAppleTrustStoreAdversarial, ExplicitOversizedBundlePathFailsCleanly) {
  td::string oversized_path(8192, 'a');
  oversized_path += "/ca.pem";

  auto on_result = td::SslCtx::create(td::CSlice(oversized_path), td::SslCtx::VerifyPeer::On);
  auto off_result = td::SslCtx::create(td::CSlice(oversized_path), td::SslCtx::VerifyPeer::Off);

  ASSERT_TRUE(on_result.is_error());
  ASSERT_TRUE(off_result.is_error());
}

#endif
