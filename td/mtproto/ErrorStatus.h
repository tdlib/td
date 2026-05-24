// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/utils/Slice.h"
#include "td/utils/Status.h"

namespace td {
namespace mtproto {

constexpr int32 kMtprotoAuthKeyNotFoundErrorCode = -404;

inline bool is_mtproto_auth_key_not_found_status(const Status &status) {
  if (status.code() != kMtprotoAuthKeyNotFoundErrorCode) {
    return false;
  }

  static const Slice kMtprotoAuthKeyNotFoundPrefix("MTProto error: -404");
  auto message = status.message();
  return message.size() >= kMtprotoAuthKeyNotFoundPrefix.size() &&
         message.substr(0, kMtprotoAuthKeyNotFoundPrefix.size()) == kMtprotoAuthKeyNotFoundPrefix;
}

inline bool is_http_status_transport_error(int32 http_status_code) {
  return http_status_code >= 300;
}

inline Status make_http_status_transport_error_status(int32 http_status_code) {
  return Status::Error(http_status_code, "HTTP status code error");
}

}  // namespace mtproto
}  // namespace td