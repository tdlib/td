// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/ChaffScheduler.h"

#include <algorithm>

namespace td {
namespace mtproto {
namespace stealth {

namespace {

double max_double(double left, double right) {
  return left > right ? left : right;
}

}  // namespace

ChaffScheduler::ChaffScheduler(const StealthConfig &config, IptController &ipt_controller, IRng &rng, double now)
    : config_(config), ipt_controller_(ipt_controller), rng_(rng) {
  if (config_.chaff_policy.enabled) {
    schedule_after_activity(now);
  }
}

void ChaffScheduler::note_activity(double now) {
  if (!config_.chaff_policy.enabled) {
    return;
  }
  prune_budget_window(now);
  schedule_after_activity(now);
}

void ChaffScheduler::note_chaff_emitted(double now, size_t bytes) {
  if (!config_.chaff_policy.enabled) {
    return;
  }
  prune_budget_window(now);
  budget_window_.push_back({now, bytes});
  schedule_after_chaff(now);
}

bool ChaffScheduler::should_emit(double now, bool has_pending_data, bool can_write) const {
  if (!config_.chaff_policy.enabled || has_pending_data || !can_write || pending_target_bytes_ <= 0) {
    return false;
  }
  if (next_send_at_ == 0.0 || now + 1e-9 < next_send_at_) {
    return false;
  }
  return budget_allows(now);
}

double ChaffScheduler::get_wakeup(double now, bool has_pending_data, bool can_write) const {
  if (!config_.chaff_policy.enabled || has_pending_data || !can_write || pending_target_bytes_ <= 0 ||
      next_send_at_ == 0.0) {
    return 0.0;
  }
  auto wakeup = next_send_at_;
  auto resume_at = budget_resume_at(now);
  if (resume_at != 0.0) {
    wakeup = max_double(wakeup, resume_at);
  }
  return wakeup;
}

int32 ChaffScheduler::current_target_bytes() const {
  return pending_target_bytes_;
}

void ChaffScheduler::schedule_after_activity(double now) {
  pending_target_bytes_ = sample_target_bytes();
  next_send_at_ =
      now + static_cast<double>(config_.chaff_policy.idle_threshold_ms) / 1000.0 + sample_interval_seconds();
}

void ChaffScheduler::schedule_after_chaff(double now) {
  pending_target_bytes_ = sample_target_bytes();
  next_send_at_ = now + sample_interval_seconds();
}

void ChaffScheduler::prune_budget_window(double now) {
  while (!budget_window_.empty() && budget_window_.front().at + kBudgetWindowSeconds <= now) {
    budget_window_.pop_front();
  }
}

double ChaffScheduler::budget_resume_at(double now) const {
  uint64 bytes = 0;
  double earliest_resume = 0.0;
  for (const auto &sample : budget_window_) {
    if (sample.at + kBudgetWindowSeconds <= now) {
      continue;
    }
    bytes += sample.bytes;
    if (earliest_resume == 0.0) {
      earliest_resume = sample.at + kBudgetWindowSeconds;
    }
  }
  if (bytes + static_cast<uint64>(pending_target_bytes_) <= config_.chaff_policy.max_bytes_per_minute) {
    return 0.0;
  }
  return earliest_resume;
}

bool ChaffScheduler::budget_allows(double now) const {
  return budget_resume_at(now) == 0.0;
}

int32 ChaffScheduler::sample_target_bytes() {
  return config_.sample_chaff_record_size(rng_);
}

double ChaffScheduler::sample_interval_seconds() {
  auto sampled_delay_us = ipt_controller_.sample_idle_delay_us();
  auto min_interval_seconds = config_.chaff_policy.min_interval_ms / 1000.0;
  auto sampled_seconds = static_cast<double>(sampled_delay_us) / 1e6;
  return std::max(min_interval_seconds, sampled_seconds);
}

}  // namespace stealth
}  // namespace mtproto
}  // namespace td