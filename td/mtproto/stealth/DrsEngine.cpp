// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/DrsEngine.h"

#include <algorithm>

namespace td {
namespace mtproto {
namespace stealth {

namespace {

TrafficHint normalize_drs_hint(TrafficHint hint) {
  return hint == TrafficHint::Unknown ? TrafficHint::Interactive : hint;
}

}  // namespace

DrsEngine::DrsEngine(const DrsPolicy &policy, IRng &rng) : policy_(policy), rng_(rng) {
  CHECK(!policy_.slow_start.bins.empty());
  CHECK(!policy_.congestion_open.bins.empty());
  CHECK(!policy_.steady_state.bins.empty());
  CHECK(policy_.idle_reset_ms_min <= policy_.idle_reset_ms_max);

  auto width = static_cast<uint32>(policy_.idle_reset_ms_max - policy_.idle_reset_ms_min + 1);
  sampled_idle_reset_ms_ = policy_.idle_reset_ms_min + static_cast<int32>(rng_.bounded(width));
}

int32 DrsEngine::next_payload_cap(TrafficHint hint) {
  auto normalized_hint = normalize_drs_hint(hint);
  if (normalized_hint == TrafficHint::Keepalive || normalized_hint == TrafficHint::AuthHandshake) {
    return policy_.min_payload_cap;
  }
  if (normalized_hint == TrafficHint::BulkData) {
    return sample_from_phase(policy_.steady_state);
  }
  return sample_from_phase(phase_model());
}

void DrsEngine::notify_bytes_written(size_t bytes) {
  if (bytes == 0) {
    return;
  }
  records_in_phase_++;
  bytes_in_phase_ += bytes;
  maybe_advance_phase();
}

void DrsEngine::notify_idle() {
  phase_ = Phase::SlowStart;
  records_in_phase_ = 0;
  bytes_in_phase_ = 0;
  reset_run_state();
}

bool DrsEngine::should_reset_after_idle(double idle_seconds) const noexcept {
  return idle_seconds * 1000.0 >= static_cast<double>(sampled_idle_reset_ms_);
}

const DrsPhaseModel &DrsEngine::phase_model() const noexcept {
  switch (phase_) {
    case Phase::SlowStart:
      return policy_.slow_start;
    case Phase::CongestionOpen:
      return policy_.congestion_open;
    case Phase::SteadyState:
      return policy_.steady_state;
  }
  UNREACHABLE();
}

int32 DrsEngine::sample_from_phase(const DrsPhaseModel &model) {
  auto sampled = sample_weighted_bin_value(model);
  if (model.max_repeat_run > 0 && last_cap_ >= 0 && sampled == last_cap_ && last_cap_run_ >= model.max_repeat_run) {
    for (int attempt = 0; attempt < 8 && sampled == last_cap_; attempt++) {
      sampled = sample_weighted_bin_value(model);
    }
  }

  if (sampled == last_cap_) {
    last_cap_run_++;
  } else {
    last_cap_ = sampled;
    last_cap_run_ = 1;
  }
  return sampled;
}

int32 DrsEngine::sample_weighted_bin_value(const DrsPhaseModel &model) {
  uint32 total_weight = 0;
  for (const auto &bin : model.bins) {
    total_weight += bin.weight;
  }
  CHECK(total_weight != 0);

  uint32 selected = rng_.bounded(total_weight);
  const RecordSizeBin *chosen = &model.bins.back();
  for (const auto &bin : model.bins) {
    if (selected < bin.weight) {
      chosen = &bin;
      break;
    }
    selected -= bin.weight;
  }

  auto width = static_cast<uint32>(chosen->hi - chosen->lo + 1);
  auto sampled = chosen->lo + static_cast<int32>(rng_.bounded(width));
  if (model.local_jitter > 0) {
    auto jitter_width = static_cast<uint32>(model.local_jitter * 2 + 1);
    sampled += static_cast<int32>(rng_.bounded(jitter_width)) - model.local_jitter;
  }
  sampled = std::max(chosen->lo, std::min(sampled, chosen->hi));
  sampled = std::max(policy_.min_payload_cap, std::min(sampled, policy_.max_payload_cap));
  return sampled;
}

void DrsEngine::maybe_advance_phase() {
  switch (phase_) {
    case Phase::SlowStart:
      if (records_in_phase_ >= static_cast<size_t>(policy_.slow_start_records)) {
        phase_ = Phase::CongestionOpen;
        records_in_phase_ = 0;
        bytes_in_phase_ = 0;
        reset_run_state();
      }
      return;
    case Phase::CongestionOpen:
      if (bytes_in_phase_ >= static_cast<size_t>(policy_.congestion_bytes)) {
        phase_ = Phase::SteadyState;
        records_in_phase_ = 0;
        bytes_in_phase_ = 0;
        reset_run_state();
      }
      return;
    case Phase::SteadyState:
      return;
  }
}

void DrsEngine::reset_run_state() noexcept {
  last_cap_ = -1;
  last_cap_run_ = 0;
}

}  // namespace stealth
}  // namespace mtproto
}  // namespace td