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
using td::mtproto::stealth::RecordSizeBin;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::MockRng;

DrsPolicy make_policy() {
  DrsPhaseModel phase;
  phase.bins.push_back(RecordSizeBin{1000, 1000, 200});
  phase.bins.push_back(RecordSizeBin{1200, 1200, 8});
  phase.bins.push_back(RecordSizeBin{1400, 1400, 1});
  phase.max_repeat_run = 2;
  phase.local_jitter = 0;

  DrsPolicy policy;
  policy.slow_start = phase;
  policy.congestion_open = phase;
  policy.steady_state = phase;
  policy.slow_start_records = 1024;
  policy.congestion_bytes = 1 << 20;
  policy.idle_reset_ms_min = 1000;
  policy.idle_reset_ms_max = 1000;
  policy.min_payload_cap = 1000;
  policy.max_payload_cap = 1400;
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

TEST(RecordSizeSequenceHardeningWeightedSkewStress, RepeatBoundSurvivesLongRunsAcrossSeeds) {
  auto policy = make_policy();

  for (td::uint64 seed = 1; seed <= 64; seed++) {
    MockRng rng(seed * 19 + 5);
    DrsEngine drs(policy, rng);

    std::vector<td::int32> caps;
    caps.reserve(256);
    for (int index = 0; index < 256; index++) {
      caps.push_back(drs.next_payload_cap(TrafficHint::Interactive));
    }

    ASSERT_TRUE(max_repeat_run(caps) <= policy.slow_start.max_repeat_run);

    auto dominant_count = static_cast<int>(std::count(caps.begin(), caps.end(), 1000));
    auto medium_count = static_cast<int>(std::count(caps.begin(), caps.end(), 1200));
    auto rare_count = static_cast<int>(std::count(caps.begin(), caps.end(), 1400));
    ASSERT_TRUE(dominant_count > medium_count);
    ASSERT_TRUE(dominant_count > rare_count);
  }
}

}  // namespace