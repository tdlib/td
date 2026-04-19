// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#if TD_DARWIN && !TD_EMSCRIPTEN

#include "td/net/SslCtx.h"

#include "td/utils/common.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/path.h"
#include "td/utils/tests.h"

#include <atomic>
#include <thread>
#include <vector>

namespace {

class ScopedTempDir final {
 public:
  ScopedTempDir() {
    dir_ = td::mkdtemp(td::get_temporary_dir(), "sslctx-apple-edge").move_as_ok();
  }

  ~ScopedTempDir() {
    td::rmrf(dir_).ignore();
  }

  td::Slice path() const {
    return dir_;
  }

  td::string join(td::Slice file_name) const {
    td::string result = dir_;
    result += TD_DIR_SLASH;
    result += file_name.str();
    return result;
  }

 private:
  td::string dir_;
};

void create_empty_file(td::Slice path) {
  auto r_file = td::FileFd::open(path.str(), td::FileFd::Write | td::FileFd::Create | td::FileFd::Truncate, 0600);
  ASSERT_TRUE(r_file.is_ok());
}

}  // namespace

TEST(SslCtxAppleTrustStoreEdgeCases, ExplicitEmptyBundleFileFailsForBothModes) {
  ScopedTempDir temp_dir;
  auto empty_bundle = temp_dir.join("empty-bundle.pem");
  create_empty_file(empty_bundle);

  auto on_result = td::SslCtx::create(empty_bundle, td::SslCtx::VerifyPeer::On);
  auto off_result = td::SslCtx::create(empty_bundle, td::SslCtx::VerifyPeer::Off);

  ASSERT_TRUE(on_result.is_error());
  ASSERT_TRUE(off_result.is_error());
}

TEST(SslCtxAppleTrustStoreEdgeCases, ExplicitDirectoryPathFailsForBothModes) {
  ScopedTempDir temp_dir;

  auto on_result = td::SslCtx::create(temp_dir.path(), td::SslCtx::VerifyPeer::On);
  auto off_result = td::SslCtx::create(temp_dir.path(), td::SslCtx::VerifyPeer::Off);

  ASSERT_TRUE(on_result.is_error());
  ASSERT_TRUE(off_result.is_error());
}

TEST(SslCtxAppleTrustStoreEdgeCases, MoveOwnershipChainPreservesSingleSslCtxPointer) {
  auto initial_result = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off);
  ASSERT_TRUE(initial_result.is_ok());

  auto current = initial_result.move_as_ok();
  void *raw_ctx = current.get_openssl_ctx();
  ASSERT_TRUE(raw_ctx != nullptr);

  for (td::int32 i = 0; i < 64; ++i) {
    td::SslCtx next = std::move(current);
    ASSERT_TRUE(next.get_openssl_ctx() == raw_ctx);
    current = std::move(next);
    ASSERT_TRUE(current.get_openssl_ctx() == raw_ctx);
  }
}

TEST(SslCtxAppleTrustStoreEdgeCases, ConcurrentMixedVerifyModesNeverReturnNullContextOnSuccess) {
  constexpr td::int32 kThreadCount = 12;
  constexpr td::int32 kIterations = 128;

  std::atomic<td::int32> off_failures{0};
  std::atomic<td::int32> null_context_successes{0};
  std::vector<std::thread> workers;
  workers.reserve(static_cast<std::size_t>(kThreadCount));

  for (td::int32 i = 0; i < kThreadCount; ++i) {
    workers.emplace_back([i, &off_failures, &null_context_successes] {
      const auto mode = (i % 2 == 0) ? td::SslCtx::VerifyPeer::On : td::SslCtx::VerifyPeer::Off;
      for (td::int32 j = 0; j < kIterations; ++j) {
        auto result = td::SslCtx::create(td::CSlice(), mode);

        if (mode == td::SslCtx::VerifyPeer::Off && result.is_error()) {
          off_failures.fetch_add(1, std::memory_order_relaxed);
        }

        if (result.is_ok() && result.ok().get_openssl_ctx() == nullptr) {
          null_context_successes.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }

  for (auto &worker : workers) {
    worker.join();
  }

  ASSERT_EQ(off_failures.load(), 0);
  ASSERT_EQ(null_context_successes.load(), 0);
}

#endif
