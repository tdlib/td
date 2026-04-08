// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/StealthConfig.h"

namespace td {
namespace mtproto {
namespace stealth {

class DrsEngine final {
 public:
  enum class Phase : uint8 { SlowStart = 0, CongestionOpen = 1, SteadyState = 2 };

  DrsEngine(const DrsPolicy &policy, IRng &rng);

  int32 next_payload_cap(TrafficHint hint);
  void notify_bytes_written(size_t bytes);
  void notify_idle();

  Phase current_phase() const noexcept {
    return phase_;
  }

  bool should_reset_after_idle(double idle_seconds) const noexcept;

  int32 debug_idle_reset_ms_for_tests() const noexcept {
    return sampled_idle_reset_ms_;
  }

 private:
  const DrsPolicy policy_;
  IRng &rng_;

  Phase phase_{Phase::SlowStart};
  size_t records_in_phase_{0};
  size_t bytes_in_phase_{0};
  int32 sampled_idle_reset_ms_{0};
  int32 last_cap_{-1};
  int32 last_cap_run_{0};

  const DrsPhaseModel &phase_model() const noexcept;
  int32 sample_from_phase(const DrsPhaseModel &model);
  int32 sample_weighted_bin_value(const DrsPhaseModel &model);
  void maybe_advance_phase();
  void reset_run_state() noexcept;
};

}  // namespace stealth
}  // namespace mtproto
}  // namespace td