// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/mtproto/stealth/IptController.h"
#include "td/mtproto/stealth/StealthConfig.h"

#include <deque>

namespace td {
namespace mtproto {
namespace stealth {

class ChaffScheduler final {
 public:
  ChaffScheduler(const StealthConfig &config, IptController &ipt_controller, IRng &rng, double now);

  void note_activity(double now);
  void note_chaff_emitted(double now, size_t bytes);

  bool should_emit(double now, bool has_pending_data, bool can_write) const;
  double get_wakeup(double now, bool has_pending_data, bool can_write) const;
  int32 current_target_bytes() const;

 private:
  struct BudgetSample final {
    double at{0.0};
    size_t bytes{0};
  };

  static constexpr double kBudgetWindowSeconds = 60.0;

  void schedule_after_activity(double now);
  void schedule_after_chaff(double now);
  void prune_budget_window(double now);
  double budget_resume_at(double now) const;
  bool budget_allows(double now) const;
  int32 sample_target_bytes();
  double sample_interval_seconds();

  const StealthConfig &config_;
  IptController &ipt_controller_;
  IRng &rng_;
  std::deque<BudgetSample> budget_window_;
  double next_send_at_{0.0};
  int32 pending_target_bytes_{0};
};

}  // namespace stealth
}  // namespace mtproto
}  // namespace td