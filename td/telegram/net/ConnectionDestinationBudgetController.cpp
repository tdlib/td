// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/ConnectionDestinationBudgetController.h"

#include <algorithm>

namespace td {
namespace {

double anti_churn_delay_seconds(const mtproto::stealth::RuntimeFlowBehaviorPolicy &policy) {
  return static_cast<double>(policy.anti_churn_min_reconnect_interval_ms) / 1000.0;
}

double destination_state_retention_seconds(const mtproto::stealth::RuntimeFlowBehaviorPolicy &policy) {
  return std::max(10.0, anti_churn_delay_seconds(policy));
}

}  // namespace

double ConnectionDestinationBudgetController::destination_share_window_seconds() {
  return 10.0;
}

double ConnectionDestinationBudgetController::get_wakeup_at(double now, const DestinationKey &destination,
                                                            const mtproto::stealth::RuntimeFlowBehaviorPolicy &policy) {
  prune_old_attempts(now);
  prune_destination_state(now, policy);

  if (recent_attempts_.empty() && destination_state_.empty()) {
    return now;
  }

  double wakeup_at = now;
  size_t total_attempts = recent_attempts_.size();
  size_t destination_attempts = 0;
  size_t other_destination_attempts = 0;
  double first_destination_expiry = 0.0;
  bool has_destination_attempt = false;
  for (const auto &attempt : recent_attempts_) {
    if (attempt.destination == destination) {
      destination_attempts++;
      auto expiry_at = attempt.started_at + destination_share_window_seconds();
      if (!has_destination_attempt || expiry_at < first_destination_expiry) {
        first_destination_expiry = expiry_at;
        has_destination_attempt = true;
      }
    } else {
      other_destination_attempts++;
    }
  }

  double last_destination_started_at = -1.0;
  for (const auto &state : destination_state_) {
    if (state.destination == destination) {
      last_destination_started_at = state.last_started_at;
      break;
    }
  }

  if (last_destination_started_at >= 0.0) {
    wakeup_at = std::max(wakeup_at, last_destination_started_at + anti_churn_delay_seconds(policy));
  }

  if (destination_attempts >= static_cast<size_t>(policy.max_connects_per_10s_per_destination) &&
      has_destination_attempt) {
    wakeup_at = std::max(wakeup_at, first_destination_expiry);
  }

  if (other_destination_attempts == 0) {
    return wakeup_at;
  }

  auto projected_share = static_cast<double>(destination_attempts + 1) / static_cast<double>(total_attempts + 1);
  if (projected_share <= policy.max_destination_share || !has_destination_attempt) {
    return wakeup_at;
  }
  return std::max(wakeup_at, first_destination_expiry);
}

void ConnectionDestinationBudgetController::on_connect_started(
    double now, const DestinationKey &destination, const mtproto::stealth::RuntimeFlowBehaviorPolicy &policy) {
  prune_old_attempts(now);
  prune_destination_state(now, policy);
  recent_attempts_.push_back(Attempt{now, destination});

  auto state_it =
      std::find_if(destination_state_.begin(), destination_state_.end(),
                   [&destination](const DestinationState &state) { return state.destination == destination; });
  if (state_it == destination_state_.end()) {
    destination_state_.push_back(DestinationState{now, destination});
  } else {
    state_it->last_started_at = now;
  }
}

void ConnectionDestinationBudgetController::prune_old_attempts(double now) {
  while (!recent_attempts_.empty() && recent_attempts_.front().started_at + destination_share_window_seconds() <= now) {
    recent_attempts_.pop_front();
  }
}

void ConnectionDestinationBudgetController::prune_destination_state(
    double now, const mtproto::stealth::RuntimeFlowBehaviorPolicy &policy) {
  const auto retention_seconds = destination_state_retention_seconds(policy);
  while (!destination_state_.empty() && destination_state_.front().last_started_at + retention_seconds <= now) {
    destination_state_.pop_front();
  }
}

}  // namespace td