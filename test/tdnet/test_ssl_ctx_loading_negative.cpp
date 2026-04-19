// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

// NEGATIVE TESTS: Verify that invalid inputs are rejected correctly.
// Tests here document and protect expected-failure scenarios and confirm
// fail-closed behavior under adversarial path inputs.

#include "td/net/SslCtx.h"
#include "td/utils/tests.h"

#if !TD_EMSCRIPTEN

// NEGATIVE: Explicit path to a nonexistent file must fail regardless of verify mode.
TEST(SslCtxNegative, NonexistentExplicitPathFailsOnVerifyOn) {
  auto result = td::SslCtx::create("/does/not/exist/at/all.pem", td::SslCtx::VerifyPeer::On);
  ASSERT_TRUE(result.is_error());
}

TEST(SslCtxNegative, NonexistentExplicitPathFailsOnVerifyOff) {
  // A user-specified cert path that does not exist is always fatal,
  // even in unverified mode, because the caller made an explicit trust decision.
  auto result = td::SslCtx::create("/does/not/exist/at/all.pem", td::SslCtx::VerifyPeer::Off);
  ASSERT_TRUE(result.is_error());
}

// NEGATIVE: Path traversal attempt does not succeed when the traversed path
// does not contain a valid cert. Key invariant: no crash, clean error.
TEST(SslCtxNegative, PathTraversalToNonexistentCertFails) {
  auto result = td::SslCtx::create("../../../../../../../nonexistent.pem", td::SslCtx::VerifyPeer::On);
  ASSERT_TRUE(result.is_error());
}

// NEGATIVE: An extremely long path (well beyond PATH_MAX) must not crash or overflow.
TEST(SslCtxNegative, ExtremelyLongPathDoesNotCrash) {
  td::string long_path(8192, 'x');
  long_path += "/cert.pem";
  auto result = td::SslCtx::create(td::CSlice(long_path), td::SslCtx::VerifyPeer::On);
  // Must not crash; result is either ok or error (likely error).
  ASSERT_FALSE(result.is_ok() && result.ok().get_openssl_ctx() == nullptr);
}

// NEGATIVE: Repeated failure under VerifyPeer::On must return consistent error codes,
// not alternate between success and failure (no intermittent/non-deterministic behavior).
TEST(SslCtxNegative, RepeatedFailuresAreDeterministic) {
  bool first_ok = td::SslCtx::create("/no/such/cert.pem", td::SslCtx::VerifyPeer::On).is_ok();
  for (int i = 0; i < 5; ++i) {
    bool ok = td::SslCtx::create("/no/such/cert.pem", td::SslCtx::VerifyPeer::On).is_ok();
    ASSERT_EQ(first_ok, ok);
  }
}

// NEGATIVE: Binary junk as cert content must be rejected cleanly (not crash).
// We use /dev/urandom as a worst-case binary cert path — it exists but contains noise.
TEST(SslCtxNegative, BinaryCertContentRejectedCleanly) {
  // /dev/urandom always exists and is readable but is never a valid cert.
  auto result = td::SslCtx::create("/dev/urandom", td::SslCtx::VerifyPeer::On);
  ASSERT_TRUE(result.is_error());
}

// NEGATIVE: NUL byte embedded in cert path must not truncate to a different valid path.
// The SslCtx interface takes a CSlice which stops at the embedded NUL, so the
// path becomes "/tmp/" which would succeed (if it exists) or fail — no valid cert.
TEST(SslCtxNegative, EmbeddedNulInCertPathHandledSafely) {
  // Construct a string with embedded NUL: would truncate to "/tmp/"
  td::string path_with_nul = "/tmp/\0evil.pem";  // intentional embedded NUL
  auto result = td::SslCtx::create(td::CSlice(path_with_nul), td::SslCtx::VerifyPeer::On);
  ASSERT_TRUE(result.is_error());
}

#endif  // !TD_EMSCRIPTEN
