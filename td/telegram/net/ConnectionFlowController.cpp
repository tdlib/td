//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/net/ConnectionFlowController.h"

#include <algorithm>

namespace td {
namespace {

constexpr double kConnectWindowSeconds = 10.0;

double anti_churn_delay_seconds(const mtproto::stealth::RuntimeFlowBehaviorPolicy &policy) {
  return static_cast<double>(policy.anti_churn_min_reconnect_interval_ms) / 1000.0;
}

}  // namespace

double ConnectionFlowController::get_wakeup_at(double now, const mtproto::stealth::RuntimeFlowBehaviorPolicy &policy) {
  prune_old_attempts(now);

  double wakeup_at = now;
  if (last_connect_started_at_ >= 0.0) {
    wakeup_at = std::max(wakeup_at, last_connect_started_at_ + anti_churn_delay_seconds(policy));
  }
  if (recent_connect_attempts_.size() >= policy.max_connects_per_10s_per_destination) {
    wakeup_at = std::max(wakeup_at, recent_connect_attempts_.front() + kConnectWindowSeconds);
  }
  return wakeup_at;
}

void ConnectionFlowController::on_connect_started(double now,
                                                  const mtproto::stealth::RuntimeFlowBehaviorPolicy &policy) {
  prune_old_attempts(now);
  last_connect_started_at_ = now;
  recent_connect_attempts_.push_back(now);

  const auto max_attempts = static_cast<size_t>(policy.max_connects_per_10s_per_destination);
  if (recent_connect_attempts_.size() > max_attempts) {
    recent_connect_attempts_.erase(
        recent_connect_attempts_.begin(),
        recent_connect_attempts_.begin() + static_cast<td::int64>(recent_connect_attempts_.size() - max_attempts));
  }
}

void ConnectionFlowController::prune_old_attempts(double now) {
  auto first_valid = std::find_if(recent_connect_attempts_.begin(), recent_connect_attempts_.end(),
                                  [now](double attempt_at) { return attempt_at + kConnectWindowSeconds > now; });
  recent_connect_attempts_.erase(recent_connect_attempts_.begin(), first_valid);
}

}  // namespace td