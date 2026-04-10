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

constexpr int32 kCandidateSampleAttempts = 32;
constexpr int32 kMaxMonotonicDirectionRun = 2;

TrafficHint normalize_drs_hint(TrafficHint hint) {
  return hint == TrafficHint::Unknown ? TrafficHint::Interactive : hint;
}

int8 direction_of_delta(int32 from, int32 to) {
  if (to > from) {
    return 1;
  }
  if (to < from) {
    return -1;
  }
  return 0;
}

int64 phase_transition_lower_bound(int32 anchor) {
  return static_cast<int64>(anchor) / 3 + 1;
}

int64 phase_transition_upper_bound(int32 anchor) {
  return static_cast<int64>(anchor) * 3 - 1;
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
  transition_anchor_cap_ = -1;
  reset_run_state();
}

void DrsEngine::prime_with_payload_cap(int32 payload_cap) noexcept {
  transition_anchor_cap_ = -1;
  previous_cap_ = -1;
  monotonic_run_ = 0;
  last_direction_ = 0;
  last_cap_ = payload_cap;
  last_cap_run_ = 1;
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
  auto best_score = score_candidate(model, sampled);
  for (int attempt = 1; attempt < kCandidateSampleAttempts; attempt++) {
    auto candidate = sample_weighted_bin_value(model);
    auto candidate_score = score_candidate(model, candidate);
    if (candidate_score < best_score) {
      sampled = candidate;
      best_score = candidate_score;
      if (best_score == 0) {
        break;
      }
    }
  }

  if (best_score > 0) {
    auto consider_candidate = [&](int32 candidate) {
      auto candidate_score = score_candidate(model, candidate);
      if (candidate_score < best_score) {
        sampled = candidate;
        best_score = candidate_score;
      }
      return best_score == 0;
    };

    // When weighted sampling keeps landing on a penalized cap, scan bin structure to preserve hardening bounds.
    for (const auto &bin : model.bins) {
      auto lo = std::max(bin.lo, policy_.min_payload_cap);
      auto hi = std::min(bin.hi, policy_.max_payload_cap);
      if (lo > hi) {
        continue;
      }

      if (consider_candidate(lo) || consider_candidate(hi)) {
        break;
      }

      if (last_cap_ >= 0) {
        auto anchored = std::max(lo, std::min(last_cap_, hi));
        if (consider_candidate(anchored)) {
          break;
        }
        if (anchored > lo && consider_candidate(anchored - 1)) {
          break;
        }
        if (anchored < hi && consider_candidate(anchored + 1)) {
          break;
        }
      }

      if (transition_anchor_cap_ >= 0) {
        auto lower_bound = phase_transition_lower_bound(transition_anchor_cap_);
        auto upper_bound = phase_transition_upper_bound(transition_anchor_cap_);
        auto bounded_lo = static_cast<int64>(lo);
        auto bounded_hi = static_cast<int64>(hi);
        if (lower_bound <= bounded_hi && upper_bound >= bounded_lo) {
          auto transition_lo = static_cast<int32>(std::max<int64>(bounded_lo, lower_bound));
          auto transition_hi = static_cast<int32>(std::min<int64>(bounded_hi, upper_bound));
          if (consider_candidate(transition_lo) || consider_candidate(transition_hi)) {
            break;
          }
          if (last_cap_ >= 0) {
            auto transition_anchored = std::max(transition_lo, std::min(last_cap_, transition_hi));
            if (consider_candidate(transition_anchored)) {
              break;
            }
          }
        }
      }
    }
  }

  sampled = smooth_phase_transition_candidate(sampled);
  note_selected_cap(sampled);
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

int32 DrsEngine::score_candidate(const DrsPhaseModel &model, int32 candidate) const noexcept {
  int32 score = 0;

  if (model.max_repeat_run > 0 && last_cap_ >= 0 && candidate == last_cap_ && last_cap_run_ >= model.max_repeat_run) {
    score += 10000;
  }

  if (last_cap_ >= 0) {
    auto candidate_direction = direction_of_delta(last_cap_, candidate);
    if (candidate_direction != 0 && candidate_direction == last_direction_ &&
        monotonic_run_ >= kMaxMonotonicDirectionRun) {
      score += 2000 * monotonic_run_;
    }
  }

  if (transition_anchor_cap_ >= 0) {
    auto lower_bound = phase_transition_lower_bound(transition_anchor_cap_);
    auto upper_bound = phase_transition_upper_bound(transition_anchor_cap_);
    auto candidate64 = static_cast<int64>(candidate);
    if (candidate64 > upper_bound) {
      score += static_cast<int32>(candidate64 - upper_bound);
    } else if (candidate64 < lower_bound) {
      score += static_cast<int32>(lower_bound - candidate64);
    }
  }

  return score;
}

int32 DrsEngine::smooth_phase_transition_candidate(int32 candidate) noexcept {
  if (transition_anchor_cap_ < 0) {
    return candidate;
  }

  auto anchor = transition_anchor_cap_;
  transition_anchor_cap_ = -1;
  auto lower_bound = phase_transition_lower_bound(anchor);
  auto upper_bound = phase_transition_upper_bound(anchor);
  auto candidate64 = static_cast<int64>(candidate);
  if (candidate64 >= lower_bound && candidate64 <= upper_bound) {
    return candidate;
  }

  auto smoothed = static_cast<int64>(anchor) + (candidate64 - static_cast<int64>(anchor)) / 2;
  auto bounded = std::max(lower_bound, std::min(smoothed, upper_bound));
  return static_cast<int32>(
      std::max<int64>(policy_.min_payload_cap, std::min<int64>(bounded, policy_.max_payload_cap)));
}

void DrsEngine::maybe_advance_phase() {
  switch (phase_) {
    case Phase::SlowStart:
      if (records_in_phase_ >= static_cast<size_t>(policy_.slow_start_records)) {
        transition_anchor_cap_ = last_cap_;
        phase_ = Phase::CongestionOpen;
        records_in_phase_ = 0;
        bytes_in_phase_ = 0;
        reset_run_state();
      }
      return;
    case Phase::CongestionOpen:
      if (bytes_in_phase_ >= static_cast<size_t>(policy_.congestion_bytes)) {
        transition_anchor_cap_ = last_cap_;
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

void DrsEngine::note_selected_cap(int32 cap) noexcept {
  auto direction = last_cap_ >= 0 ? direction_of_delta(last_cap_, cap) : 0;
  if (direction != 0) {
    if (direction == last_direction_) {
      monotonic_run_++;
    } else {
      monotonic_run_ = 1;
      last_direction_ = static_cast<int8>(direction);
    }
  } else {
    monotonic_run_ = 0;
    last_direction_ = 0;
  }

  previous_cap_ = last_cap_;
  if (cap == last_cap_) {
    last_cap_run_++;
  } else {
    last_cap_ = cap;
    last_cap_run_ = 1;
  }
}

void DrsEngine::reset_run_state() noexcept {
  previous_cap_ = -1;
  last_cap_ = -1;
  last_cap_run_ = 0;
  monotonic_run_ = 0;
  last_direction_ = 0;
}

}  // namespace stealth
}  // namespace mtproto
}  // namespace td