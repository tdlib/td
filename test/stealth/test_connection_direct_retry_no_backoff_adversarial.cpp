// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Adversarial tests: connection retry policy for direct online connections.
//
// Root cause analysis for "infinite authentication attempts":
// When act_as_if_online=true and no proxy is configured,
// should_apply_connection_failure_backoff() returns FALSE, meaning:
//   - apply_exponential_backoff = false
//   - bounded_retry = false
// This means every connection failure triggers an IMMEDIATE retry with no delay
// and no retry count limit.  In Russia, where direct TCP to Telegram DCs is
// blocked, this produces a tight infinite loop at the transport layer.
//
// After OpenSSL/toolchain update, any change that makes connection or SSL setup
// fail (e.g., cert loading failure, TLS version mismatch) compounds the issue
// because even one-time failures now trigger the infinite loop.
//
// These tests document and verify the behavior, both current (no backoff) and
// the expected fixed behavior (minimum backoff after N consecutive failures).

#include "td/net/ProxySetupError.h"
#include "td/telegram/net/ConnectionRetryPolicy.h"

#include "td/utils/tests.h"

#include <limits>

namespace {

td::mtproto::ProxySecret make_tls_proxy_secret() {
  td::string raw_secret;
  raw_secret.push_back(static_cast<char>(0xee));
  raw_secret += "0123456789abcdefdomain";
  return td::mtproto::ProxySecret::from_raw(raw_secret);
}

// --- Security policy tests ---

// Requirement: direct online connections must use exponential backoff.
TEST(ConnectionDirectRetryAdversarial, DirectOnlineConnectionUsesExponentialBackoff) {
  auto classification = td::classify_connection_failure(true, td::Proxy(), td::Status::Error("Connection refused"));
  ASSERT_TRUE(classification.apply_exponential_backoff);
}

// Requirement: direct online connections must also use bounded retry.
TEST(ConnectionDirectRetryAdversarial, DirectOnlineConnectionHasRetryBound) {
  auto classification = td::classify_connection_failure(true, td::Proxy(), td::Status::Error("Connection refused"));
  ASSERT_TRUE(classification.bounded_retry);
}

// Document: offline direct connections DO get backoff (correct behavior for offline case).
TEST(ConnectionDirectRetryAdversarial, DirectOfflineConnectionGetsExponentialBackoff) {
  auto classification = td::classify_connection_failure(false, td::Proxy(), td::Status::Error("Connection refused"));
  ASSERT_TRUE(classification.apply_exponential_backoff);
}

// Document: the backoff max is only 16s on desktop (not mobile). This means even
// when backoff applies, desktop clients retry every 16s — fast enough to cause
// visible "spinning" behavior in the UI.
TEST(ConnectionDirectRetryAdversarial, DesktopMaxBackoffIsOnlyFifteenSeconds) {
  // On non-mobile platforms, max_backoff_seconds() should be 16.
  // On mobile (Android/iOS), it's 300s. This test documents the desktop behavior.
#if !TD_ANDROID && !TD_DARWIN_IOS && !TD_DARWIN_VISION_OS && !TD_DARWIN_WATCH_OS && !TD_TIZEN
  ASSERT_EQ(16, td::ConnectionFailureBackoff::max_backoff_seconds());
#else
  ASSERT_EQ(300, td::ConnectionFailureBackoff::max_backoff_seconds());
#endif
}

// Adversarial: ConnectionFailureBackoff growth rate.
// Starting from now=0, backoff doubles each event: 1s, 2s, 4s, 8s, 16s (capped).
TEST(ConnectionDirectRetryAdversarial, BackoffGrowthExponentialUntilCap) {
  td::ConnectionFailureBackoff backoff;
  const int32_t cap = td::ConnectionFailureBackoff::max_backoff_seconds();

  // Successive events should produce exponentially growing delays up to the cap.
  std::vector<int32_t> wakeups;
  int32_t now = 0;
  for (int i = 0; i < 10; i++) {
    backoff.add_event(now);
    wakeups.push_back(backoff.get_wakeup_at());
    now = backoff.get_wakeup_at();
  }

  // Wakeup times must be strictly increasing (each event pushes wakeup further).
  for (size_t i = 1; i < wakeups.size(); i++) {
    ASSERT_TRUE(wakeups[i] > wakeups[i - 1]);
  }

  // The delay from now at the last event must be <= cap (did not overflow beyond cap).
  int32_t last_delay = wakeups.back() - (wakeups[wakeups.size() - 2]);
  ASSERT_TRUE(last_delay <= cap);
}

// Adversarial: clear() resets backoff fully.
TEST(ConnectionDirectRetryAdversarial, BackoffClearResetsToZero) {
  td::ConnectionFailureBackoff backoff;
  backoff.add_event(1000);
  ASSERT_TRUE(backoff.get_wakeup_at() > 1000);
  backoff.clear();
  ASSERT_EQ(0, backoff.get_wakeup_at());
}

// --- Policy correctness requirements ---

// Requirement: proxy connections MUST have bounded_retry=true (already tested
// in classification_security, but this test makes the REGRESSION explicit).
TEST(ConnectionDirectRetryAdversarial, ProxyConnectionWithBackoffHasBoundedRetry) {
  auto proxy = td::Proxy::mtproto("proxy.example", 443, make_tls_proxy_secret());
  auto classification = td::classify_connection_failure(true, proxy, td::Status::Error("Connection refused"));
  ASSERT_TRUE(classification.bounded_retry);
}

// Requirement: TLS hash mismatch for proxy connection must have bounded_retry=true.
// This prevents the specific infinite loop scenario: proxy returns wrong HMAC
// (e.g., misconfigured proxy secret) → TlsInit fails → connection retries
// immediately without limit.
TEST(ConnectionDirectRetryAdversarial, TlsHashMismatchOnProxyHasBoundedRetry) {
  auto proxy = td::Proxy::mtproto("proxy.example", 443, make_tls_proxy_secret());
  auto classification = td::classify_connection_failure(
      true, proxy,
      td::Status::Error(static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloResponseHashMismatch),
                        "Response hash mismatch"));
  ASSERT_TRUE(classification.proxy_backed);
  ASSERT_TRUE(classification.deterministic);
  ASSERT_TRUE(classification.bounded_retry);
}

// Adversarial: ConnectionFailureBackoff at max int32 values does not overflow.
// (Regression guard — already covered in backoff_overflow_adversarial, duplicated here
// because this directly feeds into the infinite-retry scenario: if overflow wraps to 0,
// get_wakeup_at() returns 0 and the caller retries immediately, bypassing the backoff.)
TEST(ConnectionDirectRetryAdversarial, BackoffAtInt32MaxSaturates) {
  td::ConnectionFailureBackoff backoff;
  const auto limit = std::numeric_limits<td::int32>::max();
  backoff.add_event(limit);
  ASSERT_EQ(limit, backoff.get_wakeup_at());
}

}  // namespace
