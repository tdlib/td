// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/utils/common.h"

namespace td {

enum class ActiveConnectionLifecycleRole : uint8 {
  Main = 0,
  LongPoll = 1,
  Upload = 2,
  Download = 3,
  Cached = 4,
};

enum class ActiveConnectionLifecycleState : uint8 {
  Warmup = 0,
  Eligible = 1,
  RotationPending = 2,
  Draining = 3,
  Retired = 4,
};

enum class ActiveConnectionRotationExemptionReason : uint8 {
  None = 0,
  Warmup = 1,
  AuthHandshake = 2,
  Shutdown = 3,
  DestinationBudget = 4,
  AntiChurn = 5,
  UnsafeHandoverPoint = 6,
};

struct ActiveConnectionLifecyclePolicy final {
  uint32 hard_ceiling_ms{0};
  uint32 overlap_max_ms{0};
  uint32 rotation_backoff_ms{0};
  uint32 max_overlap_connections_per_destination{1};
  bool enable_active_rotation{false};
};

struct ActiveConnectionLifecycleInput final {
  uint64 now_ms{0};
  bool has_inflight_queries{false};
  bool auth_in_progress{false};
  bool shutdown_requested{false};
  bool unsafe_handover_point{false};
  bool destination_budget_allows_overlap{true};
  bool anti_churn_allows_rotation{true};
};

struct ActiveConnectionLifecycleDecision final {
  bool prepare_successor{false};
  bool route_new_queries_to_successor{false};
  bool retire_current{false};
  bool over_age_degraded{false};
};

class ActiveConnectionLifecycleStateMachine final {
 public:
  ActiveConnectionLifecycleStateMachine(ActiveConnectionLifecycleRole role, uint64 opened_at_ms,
                                        uint64 rotate_after_at_ms) noexcept;

  void rearm(ActiveConnectionLifecycleRole role, uint64 opened_at_ms, uint64 rotate_after_at_ms) noexcept;
  void mark_eligible() noexcept;
  bool mark_successor_ready(uint64 now_ms) noexcept;
  void mark_successor_failed(uint64 now_ms, uint32 rotation_backoff_ms,
                             ActiveConnectionRotationExemptionReason reason) noexcept;

  ActiveConnectionLifecycleDecision poll(const ActiveConnectionLifecyclePolicy &policy,
                                         const ActiveConnectionLifecycleInput &input) noexcept;

  ActiveConnectionLifecycleRole role() const noexcept {
    return role_;
  }

  ActiveConnectionLifecycleState state() const noexcept {
    return state_;
  }

  uint32 rotation_attempts() const noexcept {
    return rotation_attempts_;
  }

  bool has_successor() const noexcept {
    return has_successor_;
  }

  bool is_over_age_degraded() const noexcept {
    return over_age_degraded_;
  }

  uint64 draining_started_at_ms() const noexcept {
    return draining_started_at_ms_;
  }

  ActiveConnectionRotationExemptionReason rotation_exemption_reason() const noexcept {
    return rotation_exemption_reason_;
  }

 private:
  bool is_rotation_due(uint64 now_ms) const noexcept;
  bool is_hard_ceiling_reached(const ActiveConnectionLifecyclePolicy &policy, uint64 now_ms) const noexcept;
  ActiveConnectionRotationExemptionReason get_suppression_reason(
      const ActiveConnectionLifecycleInput &input) const noexcept;

  ActiveConnectionLifecycleRole role_;
  ActiveConnectionLifecycleState state_{ActiveConnectionLifecycleState::Warmup};
  ActiveConnectionRotationExemptionReason rotation_exemption_reason_{ActiveConnectionRotationExemptionReason::Warmup};
  uint64 opened_at_ms_{0};
  uint64 rotate_after_at_ms_{0};
  uint64 draining_started_at_ms_{0};
  uint64 next_rotation_retry_at_ms_{0};
  uint32 rotation_attempts_{0};
  bool has_successor_{false};
  bool cutover_pending_{false};
  bool over_age_degraded_{false};
};

}  // namespace td