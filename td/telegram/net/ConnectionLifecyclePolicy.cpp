// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/ConnectionLifecyclePolicy.h"

#include <algorithm>
#include <limits>

namespace td {

double ConnectionLifecyclePolicy::sample_active_connection_retire_at(
    double opened_at, const mtproto::stealth::RuntimeFlowBehaviorPolicy &policy, uint32 random_value) {
  const double min_lifetime_seconds = static_cast<double>(policy.min_conn_lifetime_ms) / 1000.0;
  const double max_lifetime_seconds = static_cast<double>(policy.max_conn_lifetime_ms) / 1000.0;
  const double clamped_max_lifetime_seconds = std::max(min_lifetime_seconds, max_lifetime_seconds);
  const double lifetime_range_seconds = clamped_max_lifetime_seconds - min_lifetime_seconds;
  if (lifetime_range_seconds <= 0.0) {
    return opened_at + min_lifetime_seconds;
  }

  const double fraction = static_cast<double>(random_value) / static_cast<double>(std::numeric_limits<uint32>::max());
  return opened_at + min_lifetime_seconds + lifetime_range_seconds * fraction;
}

bool ConnectionLifecyclePolicy::is_active_connection_retire_due(double retire_at, double now) {
  return retire_at > 0.0 && now >= retire_at;
}

}  // namespace td