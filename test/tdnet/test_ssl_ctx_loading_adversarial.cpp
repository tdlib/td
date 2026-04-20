// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

// ADVERSARIAL TESTS: Black-hat attack scenarios against certificate loading.
// Every test here represents a genuine threat model entry — no tautological assertions
// (i.e., no ASSERT_TRUE(x.is_ok() || x.is_error())).

#include "td/net/SslCtx.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/tests.h"

#include <array>
#include <atomic>
#include <thread>
#include <vector>

#if !TD_EMSCRIPTEN

// Attack A1: Platform identity probing via CA-load outcome consistency.
// Hypothesis: outcome differs per call, leaking platform identity.
TEST(CertificateLoadingAdversarial, PlatformIdentityProbingMustBeConsistent) {
  bool first = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off).is_ok();
  for (int i = 0; i < 5; ++i) {
    ASSERT_EQ(first, td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off).is_ok());
  }
}

// Attack A2: Nonexistent explicit cert path must fail — not silently fall back to system store.
TEST(CertificateLoadingAdversarial, NonexistentExplicitCertPathMustFail) {
  auto result = td::SslCtx::create("/probe/does/not/exist.pem", td::SslCtx::VerifyPeer::On);
  ASSERT_TRUE(result.is_error());
}

// Attack A3: VerifyPeer::Off must not silently ignore a bad explicit cert path.
// Allowing success here would mean Off mode can load SSL contexts with no trust anchors.
TEST(CertificateLoadingAdversarial, VerifyOffDoesNotIgnoreBadExplicitCertPath) {
  auto result = td::SslCtx::create("/no/cert/here.pem", td::SslCtx::VerifyPeer::Off);
  ASSERT_TRUE(result.is_error());
}

// Attack A4: Path traversal to a nonexistent path must fail, not succeed via fallback.
TEST(CertificateLoadingAdversarial, PathTraversalToNonexistentCertMustFail) {
  auto result = td::SslCtx::create("../../../../../../../nonexistent-traversal.pem", td::SslCtx::VerifyPeer::On);
  ASSERT_TRUE(result.is_error());
}

// Attack A5: Oversized path (>PATH_MAX) must not crash or overflow.
TEST(CertificateLoadingAdversarial, OversizedPathDoesNotCrashOrCorrupt) {
  td::string oversized(8192, '/');
  oversized += "cert.pem";
  auto result = td::SslCtx::create(td::CSlice(oversized), td::SslCtx::VerifyPeer::On);
  ASSERT_TRUE(result.is_error());
}

// Attack A6: Repeated create/destroy must not leak SSL_CTX references (UAF/double-free).
TEST(CertificateLoadingAdversarial, RapidAllocationDestructionDoesNotTriggerUAF) {
  for (int i = 0; i < 500; ++i) {
    auto r = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off);
    ASSERT_TRUE(r.is_ok());
    // Immediately destroyed here; SSL_CTX must be freed.
  }
}

// Attack A7: Concurrent creation must not race on shared static state (TSan target).
TEST(CertificateLoadingAdversarial, ConcurrentCreationDoesNotRaceOnSharedState) {
  constexpr int kThreads = 8;
  std::atomic<int> failures{0};
  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&]() {
      for (int j = 0; j < 20; ++j) {
        if (!td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off).is_ok()) {
          failures.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }
  for (auto &t : threads) {
    t.join();
  }
  ASSERT_EQ(failures.load(), 0);
}

// Attack A8: Interleaved On/Off mode loads must not cause verify-mode cross-contamination.
TEST(CertificateLoadingAdversarial, InterleavedVerifyModesDoNotCrossContaminate) {
  for (int i = 0; i < 10; ++i) {
    auto on_result = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::On);
    auto off_result = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off);
    // Off-mode must always succeed.
    ASSERT_TRUE(off_result.is_ok());
    if (on_result.is_ok() && off_result.is_ok()) {
      // They must be different SSL_CTX instances.
      ASSERT_TRUE(on_result.ok().get_openssl_ctx() != off_result.ok().get_openssl_ctx());
    }
  }
}

// Attack A9: Format-string special chars in path must not corrupt state.
TEST(CertificateLoadingAdversarial, FormatStringCharsInPathDoNotCorruptState) {
  // If the path reaches sprintf-like logging without sanitization, %n causes corruption.
  auto result = td::SslCtx::create("/tmp/%s/%d/%n/../../../../etc/passwd.pem", td::SslCtx::VerifyPeer::On);
  ASSERT_TRUE(result.is_error());
}

// Attack A10: Empty-path On-mode must produce either a valid context or a clean error.
// What must NOT happen: is_ok() with null SSL_CTX pointer (silent empty trust store).
TEST(CertificateLoadingAdversarial, EmptyPathOnModeNeverSilentlyEmptyContext) {
  auto result = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::On);
  if (result.is_ok()) {
    ASSERT_TRUE(result.ok().get_openssl_ctx() != nullptr);
  }
  // If error, that is also acceptable (system store may be absent).
}

// Attack A11: Repeated loads must not accumulate memory (leak detector target).
TEST(CertificateLoadingAdversarial, RepeatedLoadsDoNotLeakMemory) {
  for (int i = 0; i < 200; ++i) {
    auto result = td::SslCtx::create(td::CSlice(), td::SslCtx::VerifyPeer::Off);
    ASSERT_TRUE(result.is_ok());
  }
}

// Attack A12: Embedded NUL in explicit cert path must be rejected. Otherwise,
// an attacker can smuggle a trusted prefix path and hide a malicious suffix.
TEST(CertificateLoadingAdversarial, ExplicitPathWithEmbeddedNulIsRejectedFailClosed) {
  static constexpr std::array<td::CSlice, 3> kCandidates = {
      "/etc/ssl/certs/ca-certificates.crt",
      "/etc/pki/tls/certs/ca-bundle.crt",
      "/etc/ssl/ca-bundle.pem",
  };

  td::string trusted_bundle;
  for (auto candidate : kCandidates) {
    auto r_file = td::FileFd::open(candidate.str(), td::FileFd::Read);
    if (r_file.is_ok()) {
      trusted_bundle = candidate.str();
      break;
    }
  }
  if (trusted_bundle.empty()) {
    return;
  }

  td::string tainted = trusted_bundle;
  tainted += '\0';
  tainted += "/attacker-controlled-suffix.pem";

  auto on_result = td::SslCtx::create(td::CSlice(tainted), td::SslCtx::VerifyPeer::On);
  auto off_result = td::SslCtx::create(td::CSlice(tainted), td::SslCtx::VerifyPeer::Off);

  ASSERT_TRUE(on_result.is_error());
  ASSERT_TRUE(off_result.is_error());
}

#endif  // !TD_EMSCRIPTEN
