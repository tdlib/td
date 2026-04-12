// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/utils/Status.h"

namespace td {

enum class ProxySetupErrorCode : int32 {
  ConnectionClosed = -61001,
  ConnectionTimeoutExpired = -61002,
  SocksUnsupportedVersion = -61003,
  SocksUnsupportedAuthenticationMode = -61004,
  SocksUnsupportedSubnegotiationVersion = -61005,
  SocksWrongUsernameOrPassword = -61006,
  SocksConnectRejected = -61007,
  SocksInvalidResponse = -61008,
  HttpConnectRejected = -61009,
  TlsHelloWrongRegime = -61010,
  TlsHelloMalformedResponse = -61011,
  TlsHelloResponseHashMismatch = -61012,
};

inline Status make_proxy_setup_error(ProxySetupErrorCode code, Slice message) {
  return Status::Error(static_cast<int32>(code), message);
}

}  // namespace td