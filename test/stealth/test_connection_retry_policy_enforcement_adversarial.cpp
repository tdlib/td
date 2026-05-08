// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs

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

TEST(ConnectionRetryPolicyEnforcementAdversarial, DirectFailuresAlwaysEnableBackoffAndBoundedRetry) {
  auto online = td::classify_connection_failure(true, td::Proxy(), td::Status::Error("connect failure"));
  auto offline = td::classify_connection_failure(false, td::Proxy(), td::Status::Error("connect failure"));

  ASSERT_TRUE(online.apply_exponential_backoff);
  ASSERT_TRUE(online.bounded_retry);
  ASSERT_TRUE(offline.apply_exponential_backoff);
  ASSERT_TRUE(offline.bounded_retry);
}

TEST(ConnectionRetryPolicyEnforcementAdversarial, ProxyFailuresRemainBoundedAcrossDeterministicAndUnknownCases) {
  auto proxy = td::Proxy::socks5("proxy.example", 1080, "user", "password");

  auto deterministic = td::classify_connection_failure(
      true, proxy,
      td::Status::Error(static_cast<td::int32>(td::ProxySetupErrorCode::ConnectionClosed), "Connection closed"));
  auto unknown = td::classify_connection_failure(true, proxy, td::Status::Error("opaque proxy failure"));

  ASSERT_TRUE(deterministic.bounded_retry);
  ASSERT_TRUE(deterministic.apply_exponential_backoff);
  ASSERT_TRUE(unknown.bounded_retry);
  ASSERT_TRUE(unknown.apply_exponential_backoff);
}

TEST(ConnectionRetryPolicyEnforcementAdversarial, BackoffInitialDelayIsOneSecond) {
  td::ConnectionFailureBackoff backoff;
  backoff.add_event(100);
  ASSERT_EQ(101, backoff.get_wakeup_at());
}

TEST(ConnectionRetryPolicyEnforcementAdversarial, BackoffDoublesUntilDesktopCap) {
  td::ConnectionFailureBackoff backoff;
  const auto cap = td::ConnectionFailureBackoff::max_backoff_seconds();

  td::int32 now = 0;
  td::int32 previous_delay = 0;
  for (int i = 0; i < 12; i++) {
    backoff.add_event(now);
    td::int32 delay = backoff.get_wakeup_at() - now;
    ASSERT_TRUE(delay >= previous_delay);
    ASSERT_TRUE(delay <= cap);
    previous_delay = delay;
    now = backoff.get_wakeup_at();
  }
}

TEST(ConnectionRetryPolicyEnforcementAdversarial, BackoffRemainsSaturatedAtInt32MaxAcrossAdditionalEvents) {
  td::ConnectionFailureBackoff backoff;
  const auto limit = std::numeric_limits<td::int32>::max();

  backoff.add_event(limit);
  ASSERT_EQ(limit, backoff.get_wakeup_at());
  backoff.add_event(limit);
  ASSERT_EQ(limit, backoff.get_wakeup_at());
}

TEST(ConnectionRetryPolicyEnforcementAdversarial, BackoffClearRestoresInitialGrowthSequence) {
  td::ConnectionFailureBackoff backoff;

  backoff.add_event(10);  // +1
  ASSERT_EQ(11, backoff.get_wakeup_at());
  backoff.add_event(11);  // +2
  ASSERT_EQ(13, backoff.get_wakeup_at());

  backoff.clear();
  backoff.add_event(20);  // must restart from +1
  ASSERT_EQ(21, backoff.get_wakeup_at());
}

TEST(ConnectionRetryPolicyEnforcementAdversarial, BackoffHandlesNegativeTimeInputsDeterministically) {
  td::ConnectionFailureBackoff backoff;
  backoff.add_event(-10);
  ASSERT_EQ(-9, backoff.get_wakeup_at());
}

TEST(ConnectionRetryPolicyEnforcementAdversarial, ShouldApplyBackoffReturnsTrueAcrossProxyFamilies) {
  auto mtproto_proxy = td::Proxy::mtproto("proxy.example", 443, td::mtproto::ProxySecret::from_raw("0123456789abcdef"));
  auto socks_proxy = td::Proxy::socks5("proxy.example", 1080, "user", "password");
  auto http_proxy = td::Proxy::http_tcp("proxy.example", 8080, "user", "password");

  ASSERT_TRUE(td::should_apply_connection_failure_backoff(true, td::Proxy()));
  ASSERT_TRUE(td::should_apply_connection_failure_backoff(false, td::Proxy()));
  ASSERT_TRUE(td::should_apply_connection_failure_backoff(true, mtproto_proxy));
  ASSERT_TRUE(td::should_apply_connection_failure_backoff(true, socks_proxy));
  ASSERT_TRUE(td::should_apply_connection_failure_backoff(true, http_proxy));
}

TEST(ConnectionRetryPolicyEnforcementAdversarial, ClassificationKeepsUnknownStageAndReasonForGenericDirectFailure) {
  auto classification = td::classify_connection_failure(true, td::Proxy(), td::Status::Error("non-matching message"));

  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureStage::None), static_cast<td::int32>(classification.stage));
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureReason::Unknown), static_cast<td::int32>(classification.reason));
  ASSERT_TRUE(classification.bounded_retry);
}

TEST(ConnectionRetryPolicyEnforcementAdversarial, ClassificationLegacyTimeoutStringStillMapsToTransportTimeout) {
  auto proxy = td::Proxy::http_tcp("proxy.example", 8080, "user", "password");
  auto classification = td::classify_connection_failure(true, proxy, td::Status::Error("Connection timeout expired"));

  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureStage::Transport), static_cast<td::int32>(classification.stage));
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureReason::Timeout), static_cast<td::int32>(classification.reason));
  ASSERT_TRUE(classification.bounded_retry);
}

TEST(ConnectionRetryPolicyEnforcementAdversarial, ClassificationLegacyCloseStringStillMapsToImmediateClose) {
  auto proxy = td::Proxy::http_tcp("proxy.example", 8080, "user", "password");
  auto classification = td::classify_connection_failure(true, proxy, td::Status::Error("Connection closed"));

  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureStage::Transport), static_cast<td::int32>(classification.stage));
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureReason::ImmediateClose),
            static_cast<td::int32>(classification.reason));
  ASSERT_TRUE(classification.bounded_retry);
}

TEST(ConnectionRetryPolicyEnforcementAdversarial, ClassificationTlsHashMismatchRemainsDeterministicAndBounded) {
  auto proxy = td::Proxy::mtproto("proxy.example", 443, make_tls_proxy_secret());

  auto classification = td::classify_connection_failure(
      true, proxy,
      td::Status::Error(static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloResponseHashMismatch),
                        "Response hash mismatch"));

  ASSERT_TRUE(classification.deterministic);
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureStage::TlsHello), static_cast<td::int32>(classification.stage));
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureReason::ResponseHashMismatch),
            static_cast<td::int32>(classification.reason));
  ASSERT_TRUE(classification.bounded_retry);
}

}  // namespace
