// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/ConnectionRetryPolicy.h"

#include "td/net/ProxySetupError.h"

#include <algorithm>

namespace td {

void ConnectionFailureBackoff::add_event(int32 now) {
  wakeup_at_ = now + next_delay_;
  next_delay_ = std::min(max_backoff_seconds(), next_delay_ * 2);
}

int32 ConnectionFailureBackoff::max_backoff_seconds() {
#if TD_ANDROID || TD_DARWIN_IOS || TD_DARWIN_VISION_OS || TD_DARWIN_WATCH_OS || TD_TIZEN
  return 300;
#else
  return 16;
#endif
}

bool should_apply_connection_failure_backoff(bool act_as_if_online, const Proxy &proxy) {
  if (!act_as_if_online) {
    return true;
  }
  return proxy.use_proxy();
}

ConnectionFailureClassification classify_connection_failure(bool act_as_if_online, const Proxy &proxy,
                                                            const Status &status) {
  ConnectionFailureClassification result;
  result.proxy_backed = proxy.use_proxy();
  result.apply_exponential_backoff = should_apply_connection_failure_backoff(act_as_if_online, proxy);
  result.bounded_retry = result.apply_exponential_backoff;

  if (!proxy.use_proxy()) {
    return result;
  }

  switch (static_cast<ProxySetupErrorCode>(status.code())) {
    case ProxySetupErrorCode::ConnectionClosed:
      result.deterministic = true;
      result.stage = ProxyFailureStage::Transport;
      result.reason = ProxyFailureReason::ImmediateClose;
      return result;
    case ProxySetupErrorCode::ConnectionTimeoutExpired:
      result.stage = ProxyFailureStage::Transport;
      result.reason = ProxyFailureReason::Timeout;
      return result;
    case ProxySetupErrorCode::SocksUnsupportedVersion:
      result.deterministic = true;
      result.stage = ProxyFailureStage::SocksGreeting;
      result.reason = ProxyFailureReason::WrongRegime;
      return result;
    case ProxySetupErrorCode::SocksUnsupportedAuthenticationMode:
      result.deterministic = true;
      result.stage = ProxyFailureStage::SocksGreeting;
      result.reason = ProxyFailureReason::WrongRegime;
      return result;
    case ProxySetupErrorCode::SocksUnsupportedSubnegotiationVersion:
      result.deterministic = true;
      result.stage = ProxyFailureStage::SocksAuthentication;
      result.reason = ProxyFailureReason::WrongRegime;
      return result;
    case ProxySetupErrorCode::SocksWrongUsernameOrPassword:
      result.deterministic = true;
      result.stage = ProxyFailureStage::SocksAuthentication;
      result.reason = ProxyFailureReason::AuthenticationRejected;
      return result;
    case ProxySetupErrorCode::SocksConnectRejected:
      result.deterministic = true;
      result.stage = ProxyFailureStage::SocksConnect;
      result.reason = ProxyFailureReason::ConnectRejected;
      return result;
    case ProxySetupErrorCode::SocksInvalidResponse:
      result.deterministic = true;
      result.stage = ProxyFailureStage::SocksConnect;
      result.reason = ProxyFailureReason::MalformedResponse;
      return result;
    case ProxySetupErrorCode::HttpConnectRejected:
      result.deterministic = true;
      result.stage = ProxyFailureStage::HttpConnect;
      result.reason = ProxyFailureReason::ConnectRejected;
      return result;
    case ProxySetupErrorCode::TlsHelloWrongRegime:
      result.deterministic = true;
      result.stage = ProxyFailureStage::TlsHello;
      result.reason = ProxyFailureReason::WrongRegime;
      return result;
    case ProxySetupErrorCode::TlsHelloMalformedResponse:
      result.deterministic = true;
      result.stage = ProxyFailureStage::TlsHello;
      result.reason = ProxyFailureReason::MalformedResponse;
      return result;
    case ProxySetupErrorCode::TlsHelloResponseHashMismatch:
      result.deterministic = true;
      result.stage = ProxyFailureStage::TlsHello;
      result.reason = ProxyFailureReason::ResponseHashMismatch;
      return result;
  }

  if (status.message() == "Connection closed") {
    result.deterministic = true;
    result.stage = ProxyFailureStage::Transport;
    result.reason = ProxyFailureReason::ImmediateClose;
    return result;
  }
  if (status.message() == "Connection timeout expired") {
    result.stage = ProxyFailureStage::Transport;
    result.reason = ProxyFailureReason::Timeout;
    return result;
  }
  return result;
}

}  // namespace td