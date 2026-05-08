// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

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

TEST(ProxyRejectionRetryHarnessIntegration, MalformedTlsScenarioProducesTypedDeterministicReject) {
  auto status = td::test::run_tls_proxy_rejection_scenario(td::test::ProxyRejectScenario::MalformedTlsResponse);
  ASSERT_TRUE(status.is_error());
  ASSERT_TRUE(status.message().str().find("TLS hello response malformed") != td::string::npos);

  auto classification = td::classify_connection_failure(true, tls_proxy(), status);

  ASSERT_EQ(static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloMalformedResponse), status.code());
  ASSERT_TRUE(classification.is_deterministic_proxy_rejection());
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureReason::MalformedResponse),
            static_cast<td::int32>(classification.reason));
}

TEST(ProxyRejectionRetryHarnessIntegration,
     TlsFatalUnrecognizedNameAlertIsClassifiedAsDeterministicTlsMalformedResponse) {
  auto status =
      td::test::run_tls_proxy_rejection_scenario(td::test::ProxyRejectScenario::TlsFatalUnrecognizedNameAlert);
  ASSERT_TRUE(status.is_error());
  ASSERT_TRUE(status.message().str().find("TLS hello response malformed") != td::string::npos);

  auto classification = td::classify_connection_failure(true, tls_proxy(), status);

  ASSERT_EQ(static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloMalformedResponse), status.code());
  ASSERT_TRUE(classification.is_deterministic_proxy_rejection());
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureStage::TlsHello), static_cast<td::int32>(classification.stage));
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureReason::MalformedResponse),
            static_cast<td::int32>(classification.reason));
}

TEST(ProxyRejectionRetryHarnessIntegration, WrongRegimeScenarioProducesTypedDeterministicReject) {
  auto status = td::test::run_tls_proxy_rejection_scenario(td::test::ProxyRejectScenario::WrongRegimeHttpResponse);
  ASSERT_TRUE(status.is_error());
  ASSERT_TRUE(status.message().str().find("different protocol regime") != td::string::npos);

  auto classification = td::classify_connection_failure(true, tls_proxy(), status);

  ASSERT_EQ(static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloWrongRegime), status.code());
  ASSERT_TRUE(classification.is_deterministic_proxy_rejection());
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureReason::WrongRegime), static_cast<td::int32>(classification.reason));
}

TEST(ProxyRejectionRetryHarnessIntegration, ImmediateCloseScenarioProducesTypedDeterministicReject) {
  auto status = td::test::run_tls_proxy_rejection_scenario(td::test::ProxyRejectScenario::ImmediateClose);
  ASSERT_TRUE(status.is_error());

  auto classification = td::classify_connection_failure(true, tls_proxy(), status);

  ASSERT_EQ(static_cast<td::int32>(td::ProxySetupErrorCode::ConnectionClosed), status.code());
  ASSERT_TRUE(classification.is_deterministic_proxy_rejection());
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureReason::ImmediateClose),
            static_cast<td::int32>(classification.reason));
}

TEST(ProxyRejectionRetryHarnessIntegration, WrongRegimeSocksScenarioProducesTypedDeterministicReject) {
  auto status = td::test::run_tls_proxy_rejection_scenario(td::test::ProxyRejectScenario::WrongRegimeSocksResponse);
  ASSERT_TRUE(status.is_error());
  ASSERT_TRUE(status.message().str().find("different protocol regime") != td::string::npos);

  auto classification = td::classify_connection_failure(true, tls_proxy(), status);

  ASSERT_EQ(static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloWrongRegime), status.code());
  ASSERT_TRUE(classification.is_deterministic_proxy_rejection());
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureReason::WrongRegime), static_cast<td::int32>(classification.reason));
}

}  // namespace

#endif  // TD_PORT_POSIX