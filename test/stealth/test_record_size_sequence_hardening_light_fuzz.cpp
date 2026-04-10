// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/DrsEngine.h"

#include "td/utils/tests.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace {

using td::mtproto::stealth::DrsEngine;
using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::DrsPolicy;
using td::mtproto::stealth::RecordSizeBin;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::MockRng;

DrsPhaseModel make_exact_phase(std::initializer_list<td::int32> caps) {
  DrsPhaseModel phase;
  phase.max_repeat_run = 2;
  phase.local_jitter = 0;
  for (auto cap : caps) {
    phase.bins.push_back(RecordSizeBin{cap, cap, 1});
  }
  return phase;
}

DrsPolicy make_policy_for_seed(td::uint64 seed) {
  auto slow_base = static_cast<td::int32>(900 + (seed % 5) * 80);
  auto congestion_base = static_cast<td::int32>(3200 + (seed % 7) * 220);
  auto steady_base = static_cast<td::int32>(7600 + (seed % 9) * 320);

  DrsPolicy policy;
  policy.slow_start =
      make_exact_phase({slow_base, static_cast<td::int32>(slow_base + 120), static_cast<td::int32>(slow_base + 240),
                        static_cast<td::int32>(slow_base + 360)});
  policy.congestion_open =
      make_exact_phase({congestion_base, static_cast<td::int32>(congestion_base + 240),
                        static_cast<td::int32>(congestion_base + 480), static_cast<td::int32>(congestion_base + 720)});
  policy.steady_state = make_exact_phase(
      {steady_base, static_cast<td::int32>(steady_base + 1200), static_cast<td::int32>(steady_base + 2400),
       static_cast<td::int32>(steady_base + 3600), static_cast<td::int32>(steady_base + 4800)});
  policy.slow_start_records = 4;
  policy.congestion_bytes = 1;
  policy.idle_reset_ms_min = 1000;
  policy.idle_reset_ms_max = 1000;
  policy.min_payload_cap = slow_base;
  policy.max_payload_cap = steady_base + 4800;
  return policy;
}

int max_direction_run(const std::vector<td::int32> &caps) {
  int best = 0;
  int current = 0;
  int last_direction = 0;
  for (size_t i = 1; i < caps.size(); i++) {
    int direction = 0;
    if (caps[i] > caps[i - 1]) {
      direction = 1;
    } else if (caps[i] < caps[i - 1]) {
      direction = -1;
    }

    if (direction != 0 && direction == last_direction) {
      current++;
    } else if (direction != 0) {
      current = 1;
      last_direction = direction;
    } else {
      current = 0;
      last_direction = 0;
    }
    best = std::max(best, current);
  }
  return best;
}

double coefficient_of_variation(const std::vector<td::int32> &caps) {
  double mean = std::accumulate(caps.begin(), caps.end(), 0.0) / static_cast<double>(caps.size());
  CHECK(mean > 0.0);

  double variance = 0.0;
  for (auto cap : caps) {
    auto centered = static_cast<double>(cap) - mean;
    variance += centered * centered;
  }
  variance /= static_cast<double>(caps.size());
  return std::sqrt(variance) / mean;
}

TEST(RecordSizeSequenceHardeningLightFuzz, SeededPoliciesPreserveBoundaryRatioAndSteadyVariation) {
  for (td::uint64 seed = 1; seed <= 64; seed++) {
    auto policy = make_policy_for_seed(seed);
    MockRng rng(seed * 17 + 3);
    DrsEngine drs(policy, rng);

    std::vector<td::int32> caps;
    caps.reserve(54);
    for (int index = 0; index < 54; index++) {
      auto cap = drs.next_payload_cap(TrafficHint::Interactive);
      caps.push_back(cap);
      ASSERT_TRUE(cap >= policy.min_payload_cap);
      ASSERT_TRUE(cap <= policy.max_payload_cap);
      drs.notify_bytes_written(static_cast<size_t>(cap));
    }

    ASSERT_TRUE(caps[4] < caps[3] * 3);
    ASSERT_TRUE(caps[4] * 3 > caps[3]);
    ASSERT_TRUE(caps[5] < caps[4] * 3);
    ASSERT_TRUE(caps[5] * 3 > caps[4]);

    std::vector<td::int32> steady_tail(caps.begin() + 6, caps.end());
    ASSERT_TRUE(max_direction_run(steady_tail) <= 6);
    ASSERT_TRUE(coefficient_of_variation(steady_tail) > 0.08);
  }
}

}  // namespace