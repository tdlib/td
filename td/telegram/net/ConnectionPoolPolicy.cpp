//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/net/ConnectionPoolPolicy.h"

#include <algorithm>

namespace td {

double ConnectionPoolPolicy::pooled_connection_retention_seconds(
    double default_retention_seconds, const mtproto::stealth::RuntimeFlowBehaviorPolicy &policy) {
  auto max_lifetime_seconds = static_cast<double>(policy.max_conn_lifetime_ms) / 1000.0;
  return std::min(default_retention_seconds, max_lifetime_seconds);
}

bool ConnectionPoolPolicy::is_pooled_connection_expired(double pooled_at, double now, double default_retention_seconds,
                                                        const mtproto::stealth::RuntimeFlowBehaviorPolicy &policy) {
  return pooled_at < now - pooled_connection_retention_seconds(default_retention_seconds, policy);
}

}  // namespace td