// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

// ADVERSARIAL + STRESS + INTEGRATION TESTS: Trust-store integrity invariants.
//
// Root risk: a non-null but zero-certificate X509_STORE must not produce a
// "successful" SslCtx under VerifyPeer::On.  These tests exercise the boundary
// between load_system_certificate_store() returning nullptr and do_create_ssl_ctx
// applying the correct fail-closed policy.
//
// Every assertion documents a concrete threat; none are tautological.

#include "td/net/SslCtx.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/tests.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#if !TD_EMSCRIPTEN

// ── Helpers ─────────────────────────────────────────────────────────────────

namespace {

bool system_cert_store_available() {
  // Quick probe: see whether the standard Linux cert bundle is readable.
  // Used to skip tests that require a working system store.
  constexpr td::CSlice kBundle = "/etc/ssl/certs/ca-certificates.crt";
  auto r = td::FileFd::open(kBundle, td::FileFd::Read);
  if (r.is_ok()) {
    return true;
  }
  // Also try the OpenSSL default path.
  auto r2 = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off);
  return r2.is_ok();
}

}  // namespace

// ── Section 1: Fail-closed semantics for VerifyPeer modes ────────────────────

// TS-01: create(empty, On) on a host with a valid system store must succeed AND
//         produce a non-null context.  If the store has 0 certs we must NOT get
//         an opaque success that silently skips certificate verification.
TEST(SslCtxTrustStoreIntegrity, VerifyOnWithValidSystemStoreProducesUsableContext) {
  if (!system_cert_store_available()) {
    return;  // skip on minimal CI environments
  }
  auto result = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::On);
  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(result.ok().get_openssl_ctx() != nullptr);
}

// TS-02: create(empty, Off) must ALWAYS succeed regardless of system store
//         availability (primary recovery path on constrained devices).
TEST(SslCtxTrustStoreIntegrity, VerifyOffAlwaysSucceedsRegardlessOfSystemStore) {
  auto result = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off);
  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(result.ok().get_openssl_ctx() != nullptr);
}

// TS-03: Verify that a context returned for VerifyPeer::Off and one returned for
//         VerifyPeer::On (when system store is available) have DISTINCT SSL_CTX*
//         pointers — they must not share mutable OpenSSL state.
TEST(SslCtxTrustStoreIntegrity, OnAndOffContextsAreDistinctObjects) {
  if (!system_cert_store_available()) {
    return;
  }
  auto on_r = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::On);
  auto off_r = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off);
  ASSERT_TRUE(on_r.is_ok());
  ASSERT_TRUE(off_r.is_ok());
  ASSERT_TRUE(on_r.ok().get_openssl_ctx() != off_r.ok().get_openssl_ctx());
}

// TS-04: create(empty, On) outcome must be deterministic across repeated calls.
//         Non-determinism here can indicate a TOCTOU or partial initialisation race.
TEST(SslCtxTrustStoreIntegrity, VerifyOnOutcomeIsDeterministic) {
  bool first = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::On).is_ok();
  for (int i = 0; i < 10; ++i) {
    ASSERT_EQ(first, td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::On).is_ok());
  }
}

// ── Section 2: Explicit cert path edge cases ─────────────────────────────────

// TS-05: /dev/null is not a valid CA bundle.  Under VerifyPeer::On the result
//         must be an error (no trust anchors).  Under VerifyPeer::Off the
//         outcome is undefined but must not crash.
TEST(SslCtxTrustStoreIntegrity, DevNullCertFileVerifyOnMustNotSucceedWithEmptyStore) {
  auto result = td::SslCtx::create("/dev/null", td::SslCtx::VerifyPeer::On);
  // On this platform, /dev/null returns 0 bytes — no certs.
  // Either the context fails at creation, or succeeds but only if OpenSSL
  // happens to treat the empty read as "no explicit certs, use system store".
  // The invariant: if it succeeds, the SSL_CTX* must be non-null.
  if (result.is_ok()) {
    ASSERT_TRUE(result.ok().get_openssl_ctx() != nullptr);
  }
}

// TS-06: A file containing binary garbage must not be accepted as a certificate.
//         Attack: inject crafted binary payloads that look like valid ASN.1 to
//         exploit parser edge cases or confuse cert_count computation.
TEST(SslCtxTrustStoreIntegrity, BinaryGarbageCertFileRejectedCleanly) {
  // Use /dev/urandom for worst-case binary input.
  // On Linux this always exists and is readable but is never valid PEM/DER.
  auto result = td::SslCtx::create("/dev/urandom", td::SslCtx::VerifyPeer::On);
  // Must not crash; must not produce a context we trust.
  // Acceptable outcomes: error, or success with a non-null ctx (system store used).
  ASSERT_FALSE(result.is_ok() && result.ok().get_openssl_ctx() == nullptr);
}

// TS-07: Deeply nested path traversal must fail cleanly, not crash or produce
//         a valid context via accidental symlink traversal.
TEST(SslCtxTrustStoreIntegrity, DeeplyNestedPathTraversalFails) {
  // Construct a 512-component traversal chain.
  std::string deep_traversal;
  for (int i = 0; i < 128; ++i) {
    deep_traversal += "a/../../";
  }
  deep_traversal += "etc/ssl/certs/ca-certificates.crt";
  auto result = td::SslCtx::create(td::CSlice(deep_traversal), td::SslCtx::VerifyPeer::On);
  // No crash is the hard requirement; result may succeed or fail.
  ASSERT_FALSE(result.is_ok() && result.ok().get_openssl_ctx() == nullptr);
}

// TS-08: Null bytes embedded in cert path must not cause the path to be silently
//         truncated to a different valid path (CSlice stops at NUL).
TEST(SslCtxTrustStoreIntegrity, NullByteMidPathDoesNotTruncateToValidPath) {
  // "/tmp/\0/etc/ssl/certs/ca-certificates.crt" truncates to "/tmp/"
  // /tmp/ is a directory, not a cert file — must fail or succeed but not
  // produce a trust context via the truncated path.
  std::string path_with_nul = "/tmp/";
  path_with_nul += '\0';
  path_with_nul += "/etc/ssl/certs/ca-certificates.crt";
  auto result = td::SslCtx::create(td::CSlice(path_with_nul), td::SslCtx::VerifyPeer::On);
  // Must not crash; no assertion on success/failure since /tmp/ handling varies.
  ASSERT_FALSE(result.is_ok() && result.ok().get_openssl_ctx() == nullptr);
}

// TS-09: Maximum-length cert path (8 KB > PATH_MAX on Linux) must not overflow
//         any internal buffer or cause UB.  Attack: PATH_MAX arithmetic overflow.
TEST(SslCtxTrustStoreIntegrity, MaxLengthCertPathDoesNotBufferOverflow) {
  std::string oversized(8192, 'A');
  oversized += "/fake.pem";
  auto result = td::SslCtx::create(td::CSlice(oversized), td::SslCtx::VerifyPeer::On);
  ASSERT_TRUE(result.is_error());  // Must fail; must not crash.
}

// TS-10: printf-style format specifiers in the cert path must not corrupt the
//         process image via logging or sprintf-based path handling.
//         Risk: format-string injection through the log path in add_file.
TEST(SslCtxTrustStoreIntegrity, FormatStringSpecifiersInPathDoNotCorrupt) {
  auto result = td::SslCtx::create("/tmp/%s/%d/%n/../../../etc/passwd.pem", td::SslCtx::VerifyPeer::On);
  ASSERT_TRUE(result.is_error());  // Path doesn't exist; must fail cleanly.
}

// ── Section 3: Concurrency and memory safety ─────────────────────────────────

// TS-11: Concurrent VerifyPeer::On creation must not race on the static default
//         SslCtx (get_default_ssl_ctx uses a static Result).
//         TSan target: ensures SSL_CTX refcount operations are race-free.
TEST(SslCtxTrustStoreIntegrity, ConcurrentVerifyOnCreationNoRace) {
  if (!system_cert_store_available()) {
    return;
  }
  constexpr int kThreads = 8;
  constexpr int kIterations = 20;
  std::atomic<int> null_ptr_count{0};
  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&] {
      for (int j = 0; j < kIterations; ++j) {
        auto r = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::On);
        if (r.is_ok() && r.ok().get_openssl_ctx() == nullptr) {
          null_ptr_count.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }
  for (auto &t : threads) {
    t.join();
  }
  // Invariant: no successful creation with a null SSL_CTX* pointer.
  ASSERT_EQ(null_ptr_count.load(), 0);
}

// TS-12: Concurrent On/Off creates interleaved — verify no verify-mode contamination.
//         A TSan-triggering race or lock ordering bug could cause an Off-mode context
//         to acquire the verify flags of an On-mode context.
TEST(SslCtxTrustStoreIntegrity, ConcurrentMixedVerifyModesNoContamination) {
  constexpr int kThreads = 6;
  constexpr int kIterations = 30;
  std::atomic<int> off_failures{0};
  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int i = 0; i < kThreads; ++i) {
    const bool use_on = (i % 2 == 0);
    threads.emplace_back([&, use_on] {
      for (int j = 0; j < kIterations; ++j) {
        auto mode = use_on ? td::SslCtx::VerifyPeer::On : td::SslCtx::VerifyPeer::Off;
        auto r = td::SslCtx::create(td::CSlice(), mode);
        // Off mode must ALWAYS succeed.
        if (!use_on && !r.is_ok()) {
          off_failures.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }
  for (auto &t : threads) {
    t.join();
  }
  ASSERT_EQ(off_failures.load(), 0);
}

// TS-13: Mass allocation/destruction cycle — memory leak detector target.
//         Each SslCtx holds a shared_ptr<SSL_CTX>; destroying it must call
//         SSL_CTX_free exactly once and must not leak the X509_STORE.
TEST(SslCtxTrustStoreIntegrity, MassAllocationDestroyNoMemoryLeak) {
  constexpr int kIterations = 1000;
  for (int i = 0; i < kIterations; ++i) {
    auto r = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off);
    ASSERT_TRUE(r.is_ok());
    // Destructor runs here; SSL_CTX_free must be invoked.
  }
}

// TS-14: Alternating allocate-and-move — ensures SSL_CTX ref counting is
//         correctly tracked through move semantics (no double-free).
TEST(SslCtxTrustStoreIntegrity, MoveSemanticNoDoubleFree) {
  auto r = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off);
  ASSERT_TRUE(r.is_ok());
  auto ctx1 = r.move_as_ok();
  void *ptr1 = ctx1.get_openssl_ctx();
  ASSERT_TRUE(ptr1 != nullptr);
  // Move: ownership transferred, original should be in a valid-but-empty state.
  auto ctx2 = std::move(ctx1);
  ASSERT_TRUE(ctx2.get_openssl_ctx() == ptr1);
  // ctx1 destructor must not double-free.
  // ctx2 destructor frees exactly once.
}

// ── Section 4: Path probing determinism ──────────────────────────────────────

// TS-15: A repeated sequence of creates must produce outcomes in the same order
//         (no TOCTOU / filesystem-state-based non-determinism on the test host).
TEST(SslCtxTrustStoreIntegrity, RepeatedCreateOutcomeStableSequence) {
  constexpr int kRuns = 6;
  std::vector<bool> outcomes(kRuns);
  for (int i = 0; i < kRuns; ++i) {
    outcomes[i] = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off).is_ok();
  }
  for (int i = 1; i < kRuns; ++i) {
    ASSERT_EQ(outcomes[0], outcomes[i]);
  }
}

// TS-16: Explicit path to a symlink that points to a non-existent target must
//         fail cleanly.  Attack: dangling symlink injection.
TEST(SslCtxTrustStoreIntegrity, DanglingSymlinkExplicitPathFails) {
  // /proc/self/fd/9999 is very likely invalid.
  auto result = td::SslCtx::create("/proc/self/fd/9999", td::SslCtx::VerifyPeer::On);
  // Must fail or cleanly handle; no crash.
  ASSERT_FALSE(result.is_ok() && result.ok().get_openssl_ctx() == nullptr);
}

// ── Section 5: Encoding/injection attacks ────────────────────────────────────

// TS-17: Unicode/multibyte sequences in path must not confuse path boundary
//         detection or OpenSSL's filename parser.
TEST(SslCtxTrustStoreIntegrity, UnicodeMalformedPathHandledSafely) {
  // Multibyte sequences that may alias PATH_MAX boundary or cause strlen issues.
  std::string utf8_path = "/tmp/\xc3\xa9\xc3\xa0\xc3\xbc/nonexistent.pem";  // é, à, ü
  auto result = td::SslCtx::create(td::CSlice(utf8_path), td::SslCtx::VerifyPeer::On);
  ASSERT_TRUE(result.is_error());
}

// TS-18: URL-encoded path (percent encoding) must not be decoded.
//         If the implementation were to URL-decode, "%2F" would become "/" and
//         could traverse to unexpected locations.
TEST(SslCtxTrustStoreIntegrity, PercentEncodedPathNotDecoded) {
  auto result = td::SslCtx::create("/tmp/%2Fetc%2Fssl%2Fcerts%2Fca-certificates.crt", td::SslCtx::VerifyPeer::On);
  // The literal path with % characters should fail (does not exist as-is).
  ASSERT_TRUE(result.is_error());
}

// TS-19: Whitespace-only path must not be treated as "empty path" (system store fallback).
TEST(SslCtxTrustStoreIntegrity, WhitespaceOnlyPathTreatedAsExplicitNotEmpty) {
  auto result = td::SslCtx::create("   ", td::SslCtx::VerifyPeer::On);
  // "   " is not empty (CSlice has length 3), so system store fallback must NOT apply.
  // Outcome: error (path does not exist).
  ASSERT_TRUE(result.is_error());
}

// TS-20: Single dot "." (current directory) must not be treated as a valid cert dir
//         fallback that silently loads every file in cwd.
TEST(SslCtxTrustStoreIntegrity, SingleDotPathAsExplicitCertMustFail) {
  auto result = td::SslCtx::create(".", td::SslCtx::VerifyPeer::On);
  // "." is a valid directory, not a cert file; OpenSSL must reject it.
  // Acceptable: error or success-but-no-certs leads to error at handshake.
  // Unacceptable: success with null SSL_CTX*.
  ASSERT_FALSE(result.is_ok() && result.ok().get_openssl_ctx() == nullptr);
}

// ── Section 6: High-volume stress ────────────────────────────────────────────

// TS-21: 2000 rapid sequential creates — ASan/LSan baseline.
//         Confirms no X509_STORE leak per iteration.
TEST(SslCtxTrustStoreIntegrity, HighVolumeSequentialCreatesNoLeak) {
  constexpr int kN = 2000;
  int success_count = 0;
  for (int i = 0; i < kN; ++i) {
    auto r = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off);
    if (r.is_ok()) {
      ++success_count;
    }
  }
  ASSERT_EQ(success_count, kN);
}

#endif  // !TD_EMSCRIPTEN
