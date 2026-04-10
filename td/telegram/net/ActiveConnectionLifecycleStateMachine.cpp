// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/ActiveConnectionLifecycleStateMachine.h"

#include <limits>

namespace td {

ActiveConnectionLifecycleStateMachine::ActiveConnectionLifecycleStateMachine(ActiveConnectionLifecycleRole role,
                                                                             uint64 opened_at_ms,
                                                                             uint64 rotate_after_at_ms) noexcept
    : role_(role), opened_at_ms_(opened_at_ms), rotate_after_at_ms_(rotate_after_at_ms) {
}

void ActiveConnectionLifecycleStateMachine::rearm(ActiveConnectionLifecycleRole role, uint64 opened_at_ms,
                                                  uint64 rotate_after_at_ms) noexcept {
  role_ = role;
  state_ = ActiveConnectionLifecycleState::Warmup;
  rotation_exemption_reason_ = ActiveConnectionRotationExemptionReason::Warmup;
  opened_at_ms_ = opened_at_ms;
  rotate_after_at_ms_ = rotate_after_at_ms;
  draining_started_at_ms_ = 0;
  next_rotation_retry_at_ms_ = 0;
  rotation_attempts_ = 0;
  has_successor_ = false;
  cutover_pending_ = false;
  over_age_degraded_ = false;
}

void ActiveConnectionLifecycleStateMachine::mark_eligible() noexcept {
  if (state_ == ActiveConnectionLifecycleState::Warmup) {
    state_ = ActiveConnectionLifecycleState::Eligible;
    rotation_exemption_reason_ = ActiveConnectionRotationExemptionReason::None;
  }
}

bool ActiveConnectionLifecycleStateMachine::mark_successor_ready(uint64 now_ms) noexcept {
  if (!has_successor_ || state_ != ActiveConnectionLifecycleState::RotationPending) {
    return false;
  }
  state_ = ActiveConnectionLifecycleState::Draining;
  draining_started_at_ms_ = now_ms;
  cutover_pending_ = true;
  over_age_degraded_ = false;
  rotation_exemption_reason_ = ActiveConnectionRotationExemptionReason::None;
  return true;
}

void ActiveConnectionLifecycleStateMachine::mark_successor_failed(
    uint64 now_ms, uint32 rotation_backoff_ms, ActiveConnectionRotationExemptionReason reason) noexcept {
  if (state_ != ActiveConnectionLifecycleState::RotationPending) {
    return;
  }
  has_successor_ = false;
  cutover_pending_ = false;
  rotation_exemption_reason_ = reason;
  if (now_ms > std::numeric_limits<uint64>::max() - rotation_backoff_ms) {
    next_rotation_retry_at_ms_ = std::numeric_limits<uint64>::max();
  } else {
    next_rotation_retry_at_ms_ = now_ms + rotation_backoff_ms;
  }
}

ActiveConnectionLifecycleDecision ActiveConnectionLifecycleStateMachine::poll(
    const ActiveConnectionLifecyclePolicy &policy, const ActiveConnectionLifecycleInput &input) noexcept {
  ActiveConnectionLifecycleDecision decision;
  if (!policy.enable_active_rotation || state_ == ActiveConnectionLifecycleState::Retired) {
    return decision;
  }

  if (state_ == ActiveConnectionLifecycleState::Warmup) {
    rotation_exemption_reason_ = ActiveConnectionRotationExemptionReason::Warmup;
    return decision;
  }

  if (state_ == ActiveConnectionLifecycleState::Eligible && is_rotation_due(input.now_ms)) {
    state_ = ActiveConnectionLifecycleState::RotationPending;
  }

  if (state_ == ActiveConnectionLifecycleState::RotationPending) {
    if (cutover_pending_) {
      decision.route_new_queries_to_successor = true;
      cutover_pending_ = false;
      return decision;
    }

    const auto suppression_reason = get_suppression_reason(input);
    if (suppression_reason != ActiveConnectionRotationExemptionReason::None) {
      rotation_exemption_reason_ = suppression_reason;
      if (is_hard_ceiling_reached(policy, input.now_ms)) {
        over_age_degraded_ = true;
        decision.over_age_degraded = true;
      }
      return decision;
    }

    if (!has_successor_ && input.now_ms >= next_rotation_retry_at_ms_) {
      has_successor_ = true;
      rotation_attempts_++;
      rotation_exemption_reason_ = ActiveConnectionRotationExemptionReason::None;
      decision.prepare_successor = true;
    }
    return decision;
  }

  if (state_ == ActiveConnectionLifecycleState::Draining) {
    if (cutover_pending_) {
      decision.route_new_queries_to_successor = true;
      cutover_pending_ = false;
    }
    const bool overlap_expired =
        policy.overlap_max_ms > 0 && input.now_ms >= draining_started_at_ms_ + policy.overlap_max_ms;
    if (!input.has_inflight_queries || overlap_expired) {
      state_ = ActiveConnectionLifecycleState::Retired;
      has_successor_ = false;
      decision.retire_current = true;
    }
  }

  return decision;
}

bool ActiveConnectionLifecycleStateMachine::is_rotation_due(uint64 now_ms) const noexcept {
  return rotate_after_at_ms_ > 0 && now_ms >= rotate_after_at_ms_;
}

bool ActiveConnectionLifecycleStateMachine::is_hard_ceiling_reached(const ActiveConnectionLifecyclePolicy &policy,
                                                                    uint64 now_ms) const noexcept {
  if (policy.hard_ceiling_ms == 0) {
    return false;
  }
  if (opened_at_ms_ > std::numeric_limits<uint64>::max() - policy.hard_ceiling_ms) {
    return true;
  }
  return now_ms >= opened_at_ms_ + policy.hard_ceiling_ms;
}

ActiveConnectionRotationExemptionReason ActiveConnectionLifecycleStateMachine::get_suppression_reason(
    const ActiveConnectionLifecycleInput &input) const noexcept {
  if (input.auth_in_progress) {
    return ActiveConnectionRotationExemptionReason::AuthHandshake;
  }
  if (input.shutdown_requested) {
    return ActiveConnectionRotationExemptionReason::Shutdown;
  }
  if (!input.destination_budget_allows_overlap) {
    return ActiveConnectionRotationExemptionReason::DestinationBudget;
  }
  if (!input.anti_churn_allows_rotation) {
    return ActiveConnectionRotationExemptionReason::AntiChurn;
  }
  if (input.unsafe_handover_point) {
    return ActiveConnectionRotationExemptionReason::UnsafeHandoverPoint;
  }
  return ActiveConnectionRotationExemptionReason::None;
}

}  // namespace td