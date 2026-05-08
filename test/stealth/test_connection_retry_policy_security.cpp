// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/ConnectionRetryPolicy.h"

#include "td/utils/tests.h"

#include <string>

namespace {

TEST(ConnectionRetryPolicySecurity, OfflineDirectConnectionsKeepExponentialBackoff) {
  ASSERT_TRUE(td::should_apply_connection_failure_backoff(false, td::Proxy()));
}

TEST(ConnectionRetryPolicySecurity, OnlineDirectConnectionsUseExponentialBackoff) {
  ASSERT_TRUE(td::should_apply_connection_failure_backoff(true, td::Proxy()));
}

TEST(ConnectionRetryPolicySecurity, OnlineMtprotoProxyConnectionsUseExponentialBackoff) {
  auto proxy = td::Proxy::mtproto("proxy.example", 443, td::mtproto::ProxySecret::from_raw("0123456789abcdef"));

  ASSERT_TRUE(td::should_apply_connection_failure_backoff(true, proxy));
}

TEST(ConnectionRetryPolicySecurity, OnlineTlsEmulatedMtprotoProxyConnectionsUseExponentialBackoff) {
  std::string secret;
  secret.push_back(static_cast<char>(0xee));
  secret += "0123456789abcdefdomain";
  auto proxy = td::Proxy::mtproto("proxy.example", 443, td::mtproto::ProxySecret::from_raw(secret));

  ASSERT_TRUE(td::should_apply_connection_failure_backoff(true, proxy));
}

TEST(ConnectionRetryPolicySecurity, OnlineSocks5ProxyConnectionsUseExponentialBackoff) {
  auto proxy = td::Proxy::socks5("proxy.example", 1080, "user", "password");

  ASSERT_TRUE(td::should_apply_connection_failure_backoff(true, proxy));
}

TEST(ConnectionRetryPolicySecurity, OnlineHttpTcpProxyConnectionsUseExponentialBackoff) {
  auto proxy = td::Proxy::http_tcp("proxy.example", 8080, "user", "password");

  ASSERT_TRUE(td::should_apply_connection_failure_backoff(true, proxy));
}

}  // namespace