// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/ConnectionRetryPolicy.h"

#include "td/net/ProxySetupError.h"

#include "td/utils/tests.h"

namespace {

TEST(ConnectionRetryPolicyClassificationSecurity, DirectOnlineFailureStaysOnFastRetryPath) {
  auto classification =
      td::classify_connection_failure(true, td::Proxy(), td::Status::Error("generic connect failure"));

  ASSERT_FALSE(classification.proxy_backed);
  ASSERT_FALSE(classification.deterministic);
  ASSERT_FALSE(classification.apply_exponential_backoff);
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureStage::None), static_cast<td::int32>(classification.stage));
}

TEST(ConnectionRetryPolicyClassificationSecurity, TlsMalformedResponseIsDeterministicProxyReject) {
  auto proxy = td::Proxy::mtproto(
      "proxy.example", 443,
      td::mtproto::ProxySecret::from_raw(std::string(1, static_cast<char>(0xee)) + "0123456789abcdefdomain"));
  auto classification = td::classify_connection_failure(
      true, proxy,
      td::Status::Error(static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloMalformedResponse),
                        "First part of response to hello is invalid"));

  ASSERT_TRUE(classification.proxy_backed);
  ASSERT_TRUE(classification.deterministic);
  ASSERT_TRUE(classification.apply_exponential_backoff);
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureStage::TlsHello), static_cast<td::int32>(classification.stage));
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureReason::MalformedResponse),
            static_cast<td::int32>(classification.reason));
}

TEST(ConnectionRetryPolicyClassificationSecurity, TlsWrongRegimeIsDeterministicProxyReject) {
  auto proxy = td::Proxy::mtproto(
      "proxy.example", 443,
      td::mtproto::ProxySecret::from_raw(std::string(1, static_cast<char>(0xee)) + "0123456789abcdefdomain"));
  auto classification = td::classify_connection_failure(
      true, proxy,
      td::Status::Error(static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloWrongRegime),
                        "First part of response to hello is from the wrong protocol regime"));

  ASSERT_TRUE(classification.proxy_backed);
  ASSERT_TRUE(classification.deterministic);
  ASSERT_TRUE(classification.apply_exponential_backoff);
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureStage::TlsHello), static_cast<td::int32>(classification.stage));
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureReason::WrongRegime), static_cast<td::int32>(classification.reason));
}

TEST(ConnectionRetryPolicyClassificationSecurity, TlsHashMismatchIsDeterministicProxyReject) {
  auto proxy = td::Proxy::mtproto(
      "proxy.example", 443,
      td::mtproto::ProxySecret::from_raw(std::string(1, static_cast<char>(0xee)) + "0123456789abcdefdomain"));
  auto classification = td::classify_connection_failure(
      true, proxy,
      td::Status::Error(static_cast<td::int32>(td::ProxySetupErrorCode::TlsHelloResponseHashMismatch),
                        "Response hash mismatch"));

  ASSERT_TRUE(classification.proxy_backed);
  ASSERT_TRUE(classification.deterministic);
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureStage::TlsHello), static_cast<td::int32>(classification.stage));
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureReason::ResponseHashMismatch),
            static_cast<td::int32>(classification.reason));
}

TEST(ConnectionRetryPolicyClassificationSecurity, ConnectionCloseOnProxyPathIsDeterministicTransportReject) {
  auto proxy = td::Proxy::socks5("proxy.example", 1080, "user", "password");
  auto classification = td::classify_connection_failure(
      true, proxy,
      td::Status::Error(static_cast<td::int32>(td::ProxySetupErrorCode::ConnectionClosed), "Connection closed"));

  ASSERT_TRUE(classification.proxy_backed);
  ASSERT_TRUE(classification.deterministic);
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureStage::Transport), static_cast<td::int32>(classification.stage));
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureReason::ImmediateClose),
            static_cast<td::int32>(classification.reason));
}

TEST(ConnectionRetryPolicyClassificationSecurity, ConnectionTimeoutOnProxyPathUsesTransportTimeoutClassification) {
  auto proxy = td::Proxy::socks5("proxy.example", 1080, "user", "password");
  auto classification = td::classify_connection_failure(
      true, proxy,
      td::Status::Error(static_cast<td::int32>(td::ProxySetupErrorCode::ConnectionTimeoutExpired),
                        "Connection timeout expired"));

  ASSERT_TRUE(classification.proxy_backed);
  ASSERT_FALSE(classification.deterministic);
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureStage::Transport), static_cast<td::int32>(classification.stage));
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureReason::Timeout), static_cast<td::int32>(classification.reason));
}

TEST(ConnectionRetryPolicyClassificationSecurity, SocksWrongProtocolIsDeterministicWrongRegime) {
  auto proxy = td::Proxy::socks5("proxy.example", 1080, "user", "password");
  auto classification = td::classify_connection_failure(
      true, proxy,
      td::Status::Error(static_cast<td::int32>(td::ProxySetupErrorCode::SocksUnsupportedVersion),
                        "Unsupported socks protocol version 72"));

  ASSERT_TRUE(classification.proxy_backed);
  ASSERT_TRUE(classification.deterministic);
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureStage::SocksGreeting), static_cast<td::int32>(classification.stage));
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureReason::WrongRegime), static_cast<td::int32>(classification.reason));
}

TEST(ConnectionRetryPolicyClassificationSecurity, SocksUnsupportedAuthenticationModeIsDeterministicWrongRegime) {
  auto proxy = td::Proxy::socks5("proxy.example", 1080, "user", "password");
  auto classification = td::classify_connection_failure(
      true, proxy,
      td::Status::Error(static_cast<td::int32>(td::ProxySetupErrorCode::SocksUnsupportedAuthenticationMode),
                        "Unsupported authentication mode"));

  ASSERT_TRUE(classification.proxy_backed);
  ASSERT_TRUE(classification.deterministic);
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureStage::SocksGreeting), static_cast<td::int32>(classification.stage));
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureReason::WrongRegime), static_cast<td::int32>(classification.reason));
}

TEST(ConnectionRetryPolicyClassificationSecurity, SocksUnsupportedSubnegotiationVersionIsDeterministicWrongRegime) {
  auto proxy = td::Proxy::socks5("proxy.example", 1080, "user", "password");
  auto classification = td::classify_connection_failure(
      true, proxy,
      td::Status::Error(static_cast<td::int32>(td::ProxySetupErrorCode::SocksUnsupportedSubnegotiationVersion),
                        "Unsupported socks subnegotiation protocol version 2"));

  ASSERT_TRUE(classification.proxy_backed);
  ASSERT_TRUE(classification.deterministic);
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureStage::SocksAuthentication),
            static_cast<td::int32>(classification.stage));
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureReason::WrongRegime), static_cast<td::int32>(classification.reason));
}

TEST(ConnectionRetryPolicyClassificationSecurity, SocksWrongUsernameOrPasswordIsAuthenticationRejected) {
  auto proxy = td::Proxy::socks5("proxy.example", 1080, "user", "password");
  auto classification = td::classify_connection_failure(
      true, proxy,
      td::Status::Error(static_cast<td::int32>(td::ProxySetupErrorCode::SocksWrongUsernameOrPassword),
                        "Wrong username or password"));

  ASSERT_TRUE(classification.proxy_backed);
  ASSERT_TRUE(classification.deterministic);
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureStage::SocksAuthentication),
            static_cast<td::int32>(classification.stage));
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureReason::AuthenticationRejected),
            static_cast<td::int32>(classification.reason));
}

TEST(ConnectionRetryPolicyClassificationSecurity, SocksConnectRejectedUsesConnectRejectedReason) {
  auto proxy = td::Proxy::socks5("proxy.example", 1080, "user", "password");
  auto classification = td::classify_connection_failure(
      true, proxy,
      td::Status::Error(static_cast<td::int32>(td::ProxySetupErrorCode::SocksConnectRejected),
                        "Receive error code 5 from server"));

  ASSERT_TRUE(classification.proxy_backed);
  ASSERT_TRUE(classification.deterministic);
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureStage::SocksConnect), static_cast<td::int32>(classification.stage));
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureReason::ConnectRejected),
            static_cast<td::int32>(classification.reason));
}

TEST(ConnectionRetryPolicyClassificationSecurity, SocksInvalidResponseUsesMalformedResponseReason) {
  auto proxy = td::Proxy::socks5("proxy.example", 1080, "user", "password");
  auto classification = td::classify_connection_failure(
      true, proxy,
      td::Status::Error(static_cast<td::int32>(td::ProxySetupErrorCode::SocksInvalidResponse), "Invalid response"));

  ASSERT_TRUE(classification.proxy_backed);
  ASSERT_TRUE(classification.deterministic);
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureStage::SocksConnect), static_cast<td::int32>(classification.stage));
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureReason::MalformedResponse),
            static_cast<td::int32>(classification.reason));
}

TEST(ConnectionRetryPolicyClassificationSecurity, HttpConnectRejectIsDeterministicProxyReject) {
  auto proxy = td::Proxy::http_tcp("proxy.example", 8080, "user", "password");
  auto classification = td::classify_connection_failure(
      true, proxy,
      td::Status::Error(static_cast<td::int32>(td::ProxySetupErrorCode::HttpConnectRejected),
                        "Failed to connect to 149.154.167.50:443"));

  ASSERT_TRUE(classification.proxy_backed);
  ASSERT_TRUE(classification.deterministic);
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureStage::HttpConnect), static_cast<td::int32>(classification.stage));
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureReason::ConnectRejected),
            static_cast<td::int32>(classification.reason));
}

TEST(ConnectionRetryPolicyClassificationSecurity, UnknownProxyFailureStillFailsClosedToBackoff) {
  auto proxy = td::Proxy::socks5("proxy.example", 1080, "user", "password");
  auto classification = td::classify_connection_failure(true, proxy, td::Status::Error("opaque proxy failure"));

  ASSERT_TRUE(classification.proxy_backed);
  ASSERT_FALSE(classification.deterministic);
  ASSERT_TRUE(classification.apply_exponential_backoff);
  ASSERT_TRUE(classification.bounded_retry);
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureReason::Unknown), static_cast<td::int32>(classification.reason));
}

TEST(ConnectionRetryPolicyClassificationSecurity, LegacyStringTimeoutFallbackStillMapsToTransportTimeout) {
  auto proxy = td::Proxy::http_tcp("proxy.example", 8080, "user", "password");
  auto classification = td::classify_connection_failure(true, proxy, td::Status::Error("Connection timeout expired"));

  ASSERT_TRUE(classification.proxy_backed);
  ASSERT_FALSE(classification.deterministic);
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureStage::Transport), static_cast<td::int32>(classification.stage));
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureReason::Timeout), static_cast<td::int32>(classification.reason));
}

TEST(ConnectionRetryPolicyClassificationSecurity, LegacyStringCloseFallbackStillMapsToImmediateClose) {
  auto proxy = td::Proxy::http_tcp("proxy.example", 8080, "user", "password");
  auto classification = td::classify_connection_failure(true, proxy, td::Status::Error("Connection closed"));

  ASSERT_TRUE(classification.proxy_backed);
  ASSERT_TRUE(classification.deterministic);
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureStage::Transport), static_cast<td::int32>(classification.stage));
  ASSERT_EQ(static_cast<td::int32>(td::ProxyFailureReason::ImmediateClose),
            static_cast<td::int32>(classification.reason));
}

}  // namespace