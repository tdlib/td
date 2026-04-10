// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/DrsEngine.h"

#include "td/utils/tests.h"

#include <cmath>
#include <initializer_list>
#include <numeric>
#include <vector>

namespace {

using td::mtproto::stealth::DrsEngine;
using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::DrsPolicy;
using td::mtproto::stealth::IRng;
using td::mtproto::stealth::RecordSizeBin;
using td::mtproto::stealth::TrafficHint;

class ScriptedRng final : public IRng {
 public:
  explicit ScriptedRng(std::vector<td::uint32> script) : script_(std::move(script)) {
    CHECK(!script_.empty());
  }

  void fill_secure_bytes(td::MutableSlice dest) final {
    for (size_t index = 0; index < dest.size(); index++) {
      dest[index] = static_cast<char>(next_raw() & 0xffu);
    }
  }

  td::uint32 secure_uint32() final {
    return next_raw();
  }

  td::uint32 bounded(td::uint32 n) final {
    CHECK(n != 0u);
    return next_raw() % n;
  }

 private:
  std::vector<td::uint32> script_;
  size_t index_{0};

  td::uint32 next_raw() {
    auto value = script_[index_ % script_.size()];
    index_++;
    return value;
  }
};

DrsPhaseModel make_exact_phase(std::initializer_list<td::int32> caps) {
  DrsPhaseModel phase;
  phase.max_repeat_run = 1;
  phase.local_jitter = 0;
  for (auto cap : caps) {
    phase.bins.push_back(RecordSizeBin{cap, cap, 1});
  }
  return phase;
}

DrsPolicy make_sequence_policy(const DrsPhaseModel &phase) {
  DrsPolicy policy;
  policy.slow_start = phase;
  policy.congestion_open = phase;
  policy.steady_state = phase;
  policy.slow_start_records = 1024;
  policy.congestion_bytes = 1 << 20;
  policy.idle_reset_ms_min = 1000;
  policy.idle_reset_ms_max = 1000;
  policy.min_payload_cap = phase.bins.front().lo;
  policy.max_payload_cap = phase.bins.back().hi;
  return policy;
}

std::vector<td::int32> sample_caps(DrsEngine &drs, size_t count) {
  std::vector<td::int32> caps;
  caps.reserve(count);
  for (size_t i = 0; i < count; i++) {
    caps.push_back(drs.next_payload_cap(TrafficHint::Interactive));
  }
  return caps;
}

double descending_pair_fraction(const std::vector<td::int32> &caps) {
  CHECK(caps.size() > 1u);
  size_t descending_pairs = 0;
  for (size_t i = 1; i < caps.size(); i++) {
    if (caps[i] < caps[i - 1]) {
      descending_pairs++;
    }
  }
  return static_cast<double>(descending_pairs) / static_cast<double>(caps.size() - 1);
}

double lag_one_autocorrelation(const std::vector<td::int32> &caps) {
  CHECK(caps.size() > 2u);
  double mean = std::accumulate(caps.begin(), caps.end(), 0.0) / static_cast<double>(caps.size());

  double variance = 0.0;
  double covariance = 0.0;
  for (size_t i = 0; i < caps.size(); i++) {
    double centered = static_cast<double>(caps[i]) - mean;
    variance += centered * centered;
    if (i > 0) {
      covariance += centered * (static_cast<double>(caps[i - 1]) - mean);
    }
  }

  CHECK(variance > 0.0);
  return covariance / variance;
}

double coefficient_of_variation(const std::vector<td::int32> &caps) {
  CHECK(!caps.empty());
  double mean = std::accumulate(caps.begin(), caps.end(), 0.0) / static_cast<double>(caps.size());
  CHECK(mean > 0.0);

  double variance = 0.0;
  for (auto cap : caps) {
    double centered = static_cast<double>(cap) - mean;
    variance += centered * centered;
  }
  variance /= static_cast<double>(caps.size());
  return std::sqrt(variance) / mean;
}

TEST(RecordSizeSequenceHardening, SequenceNotMonotonicallyIncreasing) {
  auto phase = make_exact_phase({1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700});
  auto policy = make_sequence_policy(phase);
  ScriptedRng rng({0, 1, 2, 3, 4, 5, 6, 7});
  DrsEngine drs(policy, rng);

  auto caps = sample_caps(drs, 128);

  ASSERT_TRUE(descending_pair_fraction(caps) > 0.30);
}

TEST(RecordSizeSequenceHardening, SequenceAutoCorrelationLow) {
  auto phase = make_exact_phase(
      {1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700, 1800, 1900, 2000, 2100, 2200, 2300, 2400, 2500});
  auto policy = make_sequence_policy(phase);
  ScriptedRng rng({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15});
  DrsEngine drs(policy, rng);

  auto caps = sample_caps(drs, 128);

  ASSERT_TRUE(lag_one_autocorrelation(caps) < 0.5);
}

TEST(RecordSizeSequenceHardening, PhaseTransitionNotAbrupt) {
  DrsPolicy policy;
  policy.slow_start = make_exact_phase({1200});
  policy.congestion_open = make_exact_phase({4000});
  policy.steady_state = make_exact_phase({4200, 5200, 6400, 9000});
  policy.slow_start_records = 1;
  policy.congestion_bytes = 1 << 20;
  policy.idle_reset_ms_min = 1000;
  policy.idle_reset_ms_max = 1000;
  policy.min_payload_cap = 1200;
  policy.max_payload_cap = 9000;

  ScriptedRng rng({0, 0, 0, 0, 0, 0, 0, 0});
  DrsEngine drs(policy, rng);

  auto slow_start_cap = drs.next_payload_cap(TrafficHint::Interactive);
  drs.notify_bytes_written(static_cast<size_t>(slow_start_cap));
  auto first_congestion_cap = drs.next_payload_cap(TrafficHint::Interactive);

  ASSERT_TRUE(first_congestion_cap < slow_start_cap * 3);
}

TEST(RecordSizeSequenceHardening, SteadyStateNotFixedSize) {
  DrsPolicy policy;
  policy.slow_start = make_exact_phase({1200});
  policy.congestion_open = make_exact_phase({1800});
  policy.steady_state = make_exact_phase({3200, 4096, 6144, 8192, 10240, 12288});
  policy.slow_start_records = 1;
  policy.congestion_bytes = 1;
  policy.idle_reset_ms_min = 1000;
  policy.idle_reset_ms_max = 1000;
  policy.min_payload_cap = 1200;
  policy.max_payload_cap = 12288;

  ScriptedRng rng({0, 1, 2, 3, 4, 5});
  DrsEngine drs(policy, rng);

  drs.next_payload_cap(TrafficHint::Interactive);
  drs.notify_bytes_written(1200);
  drs.next_payload_cap(TrafficHint::Interactive);
  drs.notify_bytes_written(1);

  auto caps = sample_caps(drs, 96);

  ASSERT_TRUE(coefficient_of_variation(caps) > 0.20);
}

}  // namespace