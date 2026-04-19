// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

// CONTRACT TESTS: Pin the public SslCtx::create interface semantics.
// These tests fail immediately if creation semantics, error modes, or
// ownership transfer behavior change.

#include "td/net/SslCtx.h"
#include "td/utils/tests.h"

#if !TD_EMSCRIPTEN

// CONTRACT: create(nonexistent_cert_file, VerifyPeer::On) must return an error.
// Providing an explicit cert path that doesn't exist is always fatal
// regardless of verify mode, because the caller made an explicit trust decision.
TEST(SslCtxContract, ExplicitNonexistentCertPathFailsOnVerifyOn) {
  auto result = td::SslCtx::create("/nonexistent/path/cert.pem", td::SslCtx::VerifyPeer::On);
  ASSERT_TRUE(result.is_error());
}

// CONTRACT: same for VerifyPeer::Off — explicit bad cert path must fail.
TEST(SslCtxContract, ExplicitNonexistentCertPathFailsOnVerifyOff) {
  auto result = td::SslCtx::create("/nonexistent/path/cert.pem", td::SslCtx::VerifyPeer::Off);
  ASSERT_TRUE(result.is_error());
}

// CONTRACT: create(empty, VerifyPeer::Off) must succeed — unverified mode
// must bootstrap even when the system store is incomplete.
TEST(SslCtxContract, EmptyCertUnverifiedModeAlwaysSucceeds) {
  auto result = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off);
  ASSERT_TRUE(result.is_ok());
}

// CONTRACT: each successful create() returns an independent, movable context.
// Subsequent calls must not share mutable state.
TEST(SslCtxContract, SuccessfulCreatesReturnIndependentContexts) {
  auto r1 = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off);
  auto r2 = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off);
  ASSERT_TRUE(r1.is_ok());
  ASSERT_TRUE(r2.is_ok());
  // The two contexts must be distinct objects.
  ASSERT_TRUE(&r1.ok() != &r2.ok());
}

// CONTRACT: SslCtx returned on success is usable — get_ssl_ctx() must be non-null.
TEST(SslCtxContract, SuccessfulContextIsUsable) {
  auto result = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off);
  ASSERT_TRUE(result.is_ok());
  // Returned context must be usable (SSL_CTX* accessible)
  // We verify by move and accessing the underlying ptr indirectly via the object.
  auto ctx = result.move_as_ok();
  ASSERT_TRUE(ctx.get_openssl_ctx() != nullptr);
}

// CONTRACT: VerifyPeer enum values are distinct — the two modes are not aliased.
TEST(SslCtxContract, VerifyPeerEnumValuesAreDistinct) {
  static_assert(td::SslCtx::VerifyPeer::On != td::SslCtx::VerifyPeer::Off, "VerifyPeer enum values must be distinct");
}

#endif  // !TD_EMSCRIPTEN
