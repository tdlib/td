// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs

#include "test/stealth/ProxyRejectionTestHarness.h"

#include "td/net/ProxySetupError.h"

#include "td/utils/port/config.h"
#include "td/utils/tests.h"

#if TD_PORT_POSIX

namespace {

td::Proxy tls_proxy() {
  td::string raw_secret;
  raw_secret.push_back(static_cast<char>(0xee));
  raw_secret += "0123456789abcdefdomain";
  return td::Proxy::mtproto("proxy.example", 443, td::mtproto::ProxySecret::from_raw(raw_secret));
}

void assert_fail_closed_backoff_profile() {
  td::ConnectionFailureBackoff backoff;
  auto delays = td::test::collect_retry_delays(std::move(backoff), 5);
  ASSERT_EQ(static_cast<size_t>(5), delays.size());
  ASSERT_EQ(1, delays[0]);
  for (size_t i = 1; i < delays.size(); i++) {
    ASSERT_TRUE(delays[i] >= delays[i - 1]);
    ASSERT_TRUE(delays[i] <= td::ConnectionFailureBackoff::max_backoff_seconds());
  }
}

TEST(ProxyRejectionRetryClassificationMatrixIntegration, TlsInitTypedRejectionsMapToDeterministicFailClosedBackoff) {
  struct ScenarioCase final {
    td::test::ProxyRejectScenario scenario;
    td::int32 expected_code;
    td::ProxyFailureReason expected_reason;
  };

  const ScenarioCase cases[] = {
      {td::test::ProxyRejectScenario::MalformedTlsResponse,
       static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloMalformedResponse),
       td::ProxyFailureReason::MalformedResponse},
      {td::test::ProxyRejectScenario::TlsFatalUnrecognizedNameAlert,
       static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloMalformedResponse),
       td::ProxyFailureReason::MalformedResponse},
      {td::test::ProxyRejectScenario::WrongRegimeHttpResponse,
       static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloWrongRegime), td::ProxyFailureReason::WrongRegime},
      {td::test::ProxyRejectScenario::WrongRegimeSocksResponse,
       static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloWrongRegime), td::ProxyFailureReason::WrongRegime},
  };

  for (const auto &test_case : cases) {
    auto status = td::test::run_tls_proxy_rejection_scenario(test_case.scenario);
    ASSERT_TRUE(status.is_error());
    ASSERT_EQ(test_case.expected_code, status.code());

    auto classification = td::classify_connection_failure(true, tls_proxy(), status);
    ASSERT_TRUE(classification.proxy_backed);
    ASSERT_TRUE(classification.is_deterministic_proxy_rejection());
    ASSERT_TRUE(classification.apply_exponential_backoff);
    ASSERT_TRUE(classification.bounded_retry);
    ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureStage::TlsHello), static_cast<td::int32>(classification.stage));
    ASSERT_EQ(static_cast<td::int32>(test_case.expected_reason), static_cast<td::int32>(classification.reason));

    assert_fail_closed_backoff_profile();
  }
}

TEST(ProxyRejectionRetryClassificationMatrixIntegration, HashMismatchTypedStatusMapsToDeterministicTlsHelloAndBackoff) {
  auto status = td::Status::Error(static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloResponseHashMismatch),
                                  "Response hash mismatch");

  auto classification = td::classify_connection_failure(true, tls_proxy(), status);
  ASSERT_TRUE(classification.proxy_backed);
  ASSERT_TRUE(classification.is_deterministic_proxy_rejection());
  ASSERT_TRUE(classification.apply_exponential_backoff);
  ASSERT_TRUE(classification.bounded_retry);
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureStage::TlsHello), static_cast<td::int32>(classification.stage));
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureReason::ResponseHashMismatch),
            static_cast<td::int32>(classification.reason));

  assert_fail_closed_backoff_profile();
}

}  // namespace

#endif  // TD_PORT_POSIX
