// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

// POSITIVE TESTS: Verify the happy paths for SslCtx certificate loading.
// These tests document and protect expected-success scenarios.

#include "td/net/SslCtx.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/tests.h"

#if !TD_EMSCRIPTEN

// POSITIVE: create with empty cert path and VerifyPeer::Off succeeds on all platforms.
TEST(SslCtxPositive, UnverifiedSystemContextAlwaysSucceeds) {
  for (int i = 0; i < 3; ++i) {
    auto result = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off);
    ASSERT_TRUE(result.is_ok());
  }
}

// POSITIVE: create with an explicit valid cert file and VerifyPeer::Off succeeds.
// Uses the standard Linux cert bundle as the explicit cert path if available.
TEST(SslCtxPositive, ExplicitValidCertFileSucceedsOnVerifyOff) {
  constexpr td::CSlice kLinuxBundle = "/etc/ssl/certs/ca-certificates.crt";
  bool present = td::FileFd::open(kLinuxBundle, td::FileFd::Read).is_ok();
  if (!present) {
    return;  // No cert bundle available in this environment; skip.
  }
  auto result = td::SslCtx::create(kLinuxBundle, td::SslCtx::VerifyPeer::Off);
  ASSERT_TRUE(result.is_ok());
}

// POSITIVE: System cert path (if present) can be used directly as explicit path.
// Only runs when the standard Linux cert bundle is present.
TEST(SslCtxPositive, SystemCertBundleIfPresentCanBeUsed) {
  constexpr td::CSlice kLinuxBundle = "/etc/ssl/certs/ca-certificates.crt";
  bool present = td::FileFd::open(kLinuxBundle, td::FileFd::Read).is_ok();
  if (!present) {
    return;  // Not present in this environment; skip.
  }
  auto result = td::SslCtx::create(kLinuxBundle, td::SslCtx::VerifyPeer::Off);
  ASSERT_TRUE(result.is_ok());
}

// POSITIVE: Returned SslCtx has a non-null underlying SSL_CTX pointer.
TEST(SslCtxPositive, SuccessResultHasNonNullSslCtxPointer) {
  auto result = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off);
  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(result.ok().get_openssl_ctx() != nullptr);
}

#endif  // !TD_EMSCRIPTEN
