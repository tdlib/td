// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/DrsEngine.h"

#include "td/utils/tests.h"

#include <algorithm>
#include <vector>

namespace {

using td::mtproto::stealth::DrsEngine;
using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::DrsPolicy;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::MockRng;

DrsPolicy make_policy_for_seed(td::uint64 seed) {
  auto slow_lo = static_cast<td::int32>(900 + (seed % 4) * 40);
  auto slow_hi = static_cast<td::int32>(slow_lo + 220);
  auto congestion_lo = static_cast<td::int32>(1500 + (seed % 5) * 90);
  auto congestion_hi = static_cast<td::int32>(congestion_lo + 520);
  auto steady_lo = static_cast<td::int32>(3200 + (seed % 6) * 180);
  auto steady_hi = static_cast<td::int32>(steady_lo + 2200);

  DrsPhaseModel slow_start;
  slow_start.bins = {{slow_lo, slow_hi, 8},
                     {static_cast<td::int32>(slow_lo + 40), static_cast<td::int32>(slow_hi + 60), 2}};
  slow_start.max_repeat_run = 2;
  slow_start.local_jitter = static_cast<td::int32>(8 + (seed % 3) * 4);

  DrsPhaseModel congestion_open;
  congestion_open.bins = {
      {congestion_lo, congestion_hi, 5},
      {static_cast<td::int32>(congestion_lo + 120), static_cast<td::int32>(congestion_hi + 180), 3}};
  congestion_open.max_repeat_run = 2;
  congestion_open.local_jitter = static_cast<td::int32>(12 + (seed % 4) * 4);

  DrsPhaseModel steady_state;
  steady_state.bins = {{steady_lo, steady_hi, 7},
                       {static_cast<td::int32>(steady_lo + 400), static_cast<td::int32>(steady_hi + 800), 4},
                       {static_cast<td::int32>(steady_lo + 1200), static_cast<td::int32>(steady_hi + 1600), 1}};
  steady_state.max_repeat_run = 3;
  steady_state.local_jitter = static_cast<td::int32>(16 + (seed % 5) * 4);

  DrsPolicy policy;
  policy.slow_start = slow_start;
  policy.congestion_open = congestion_open;
  policy.steady_state = steady_state;
  policy.slow_start_records = 3;
  policy.congestion_bytes = static_cast<td::int32>(congestion_lo + congestion_hi + 256);
  policy.idle_reset_ms_min = 1000;
  policy.idle_reset_ms_max = 1000;
  policy.min_payload_cap = slow_lo;
  policy.max_payload_cap = steady_hi + 1600;
  return policy;
}

int max_repeat_run(const std::vector<td::int32> &caps) {
  int best = 0;
  int current = 0;
  int last = -1;
  for (auto cap : caps) {
    if (cap == last) {
      current++;
    } else {
      last = cap;
      current = 1;
    }
    best = std::max(best, current);
  }
  return best;
}

TEST(RecordSizeSequenceHardeningJitterRangeFuzz, RangedBinsWithJitterStayBoundedAndDiverseAcrossSeeds) {
  for (td::uint64 seed = 1; seed <= 64; seed++) {
    auto policy = make_policy_for_seed(seed);
    MockRng rng(seed * 29 + 11);
    DrsEngine drs(policy, rng);

    std::vector<td::int32> slow_caps;
    slow_caps.reserve(policy.slow_start_records);
    for (int index = 0; index < policy.slow_start_records; index++) {
      auto cap = drs.next_payload_cap(TrafficHint::Interactive);
      slow_caps.push_back(cap);
      ASSERT_TRUE(cap >= policy.min_payload_cap);
      ASSERT_TRUE(cap <= policy.max_payload_cap);
      drs.notify_bytes_written(static_cast<size_t>(cap));
    }

    auto first_congestion = drs.next_payload_cap(TrafficHint::Interactive);
    ASSERT_TRUE(first_congestion >= policy.min_payload_cap);
    ASSERT_TRUE(first_congestion <= policy.max_payload_cap);
    ASSERT_TRUE(first_congestion < slow_caps.back() * 3);
    ASSERT_TRUE(first_congestion * 3 > slow_caps.back());
    drs.notify_bytes_written(static_cast<size_t>(first_congestion));

    auto second_congestion = drs.next_payload_cap(TrafficHint::Interactive);
    ASSERT_TRUE(second_congestion >= policy.min_payload_cap);
    ASSERT_TRUE(second_congestion <= policy.max_payload_cap);
    ASSERT_TRUE(second_congestion < first_congestion * 3);
    ASSERT_TRUE(second_congestion * 3 > first_congestion);
    drs.notify_bytes_written(static_cast<size_t>(second_congestion));

    std::vector<td::int32> steady_caps;
    steady_caps.reserve(64);
    for (int index = 0; index < 64; index++) {
      auto cap = drs.next_payload_cap(TrafficHint::Interactive);
      steady_caps.push_back(cap);
      ASSERT_TRUE(cap >= policy.min_payload_cap);
      ASSERT_TRUE(cap <= policy.max_payload_cap);
    }

    ASSERT_TRUE(max_repeat_run(steady_caps) <= policy.steady_state.max_repeat_run);

    auto minmax = std::minmax_element(steady_caps.begin(), steady_caps.end());
    ASSERT_TRUE(*minmax.second - *minmax.first >= 64);

    drs.notify_idle();
    auto after_idle = drs.next_payload_cap(TrafficHint::Interactive);
    ASSERT_TRUE(after_idle >= policy.min_payload_cap);
    ASSERT_TRUE(after_idle <= policy.max_payload_cap);
  }
}

}  // namespace