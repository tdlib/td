// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/ProxyRejectionTestHarness.h"

#include "td/utils/port/config.h"
#include "td/utils/tests.h"

#if TD_PORT_POSIX

namespace {

td::Proxy tls_proxy() {
  return td::Proxy::mtproto(
      "proxy.example", 443,
      td::mtproto::ProxySecret::from_raw(std::string(1, static_cast<char>(0xee)) + "0123456789abcdefdomain"));
}

void assert_monotonic_bounded_delays(const td::vector<td::int32> &delays) {
  ASSERT_FALSE(delays.empty());
  ASSERT_EQ(1, delays[0]);
  for (size_t i = 1; i < delays.size(); i++) {
    ASSERT_TRUE(delays[i] >= delays[i - 1]);
    ASSERT_TRUE(delays[i] <= td::ConnectionFailureBackoff::max_backoff_seconds());
  }
  ASSERT_EQ(td::ConnectionFailureBackoff::max_backoff_seconds(), delays.back());
}

TEST(ProxyRejectionRetrySoak, MalformedTlsRejectionsStayMonotonicAndBounded) {
  auto status = td::test::run_tls_proxy_rejection_scenario(td::test::ProxyRejectScenario::MalformedTlsResponse);
  ASSERT_TRUE(status.is_error());

  auto classification = td::classify_connection_failure(true, tls_proxy(), status);
  ASSERT_TRUE(classification.apply_exponential_backoff);

  auto delays = td::test::collect_retry_delays(td::ConnectionFailureBackoff(), 8);
  assert_monotonic_bounded_delays(delays);
}

TEST(ProxyRejectionRetrySoak, WrongRegimeRejectionsStayMonotonicAndBounded) {
  auto status = td::test::run_tls_proxy_rejection_scenario(td::test::ProxyRejectScenario::WrongRegimeHttpResponse);
  ASSERT_TRUE(status.is_error());

  auto classification = td::classify_connection_failure(true, tls_proxy(), status);
  ASSERT_TRUE(classification.apply_exponential_backoff);

  auto delays = td::test::collect_retry_delays(td::ConnectionFailureBackoff(), 8);
  assert_monotonic_bounded_delays(delays);
}

TEST(ProxyRejectionRetrySoak, ImmediateCloseRejectionsStayMonotonicAndBounded) {
  auto status = td::test::run_tls_proxy_rejection_scenario(td::test::ProxyRejectScenario::ImmediateClose);
  ASSERT_TRUE(status.is_error());

  auto classification = td::classify_connection_failure(true, tls_proxy(), status);
  ASSERT_TRUE(classification.apply_exponential_backoff);

  auto delays = td::test::collect_retry_delays(td::ConnectionFailureBackoff(), 8);
  assert_monotonic_bounded_delays(delays);
}

TEST(ProxyRejectionRetrySoak, BackoffClearAfterSuccessRestartsFromOneSecond) {
  td::ConnectionFailureBackoff backoff;
  backoff.add_event(100);
  backoff.add_event(backoff.get_wakeup_at());
  ASSERT_TRUE(backoff.get_wakeup_at() > 101);

  backoff.clear();
  backoff.add_event(200);

  ASSERT_EQ(201, backoff.get_wakeup_at());
}

TEST(ProxyRejectionRetrySoak, BackoffSaturatesWithoutOverflowAcrossLongFailureRuns) {
  auto delays = td::test::collect_retry_delays(td::ConnectionFailureBackoff(), 64);

  ASSERT_EQ(static_cast<size_t>(64), delays.size());
  for (auto delay : delays) {
    ASSERT_TRUE(delay > 0);
    ASSERT_TRUE(delay <= td::ConnectionFailureBackoff::max_backoff_seconds());
  }
  ASSERT_EQ(td::ConnectionFailureBackoff::max_backoff_seconds(), delays.back());
}

}  // namespace

#endif  // TD_PORT_POSIX