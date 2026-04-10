// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/DrsEngine.h"

#include "td/utils/tests.h"

#include <algorithm>
#include <initializer_list>
#include <vector>

namespace {

using td::mtproto::stealth::DrsEngine;
using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::DrsPolicy;
using td::mtproto::stealth::RecordSizeBin;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::MockRng;

DrsPhaseModel make_phase(std::initializer_list<RecordSizeBin> bins) {
  DrsPhaseModel phase;
  phase.bins.assign(bins.begin(), bins.end());
  phase.max_repeat_run = 2;
  phase.local_jitter = 0;
  return phase;
}

DrsPolicy make_policy() {
  DrsPolicy policy;
  policy.slow_start = make_phase({{1000, 1000, 1}, {1120, 1120, 1}, {1240, 1240, 1}, {1360, 1360, 1}});
  policy.congestion_open = make_phase({{1800, 1800, 1}, {2200, 2200, 1}, {2600, 2600, 1}, {3000, 3000, 1}});
  policy.steady_state = make_phase(
      {{3600, 3600, 1}, {4600, 4600, 1}, {5600, 5600, 1}, {6600, 6600, 1}, {7600, 7600, 1}, {8600, 8600, 1}});
  policy.slow_start_records = 4;
  policy.congestion_bytes = 6000;
  policy.idle_reset_ms_min = 1000;
  policy.idle_reset_ms_max = 1000;
  policy.min_payload_cap = 1000;
  policy.max_payload_cap = 8600;
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

TEST(RecordSizeSequenceHardeningSeedMatrix, SeedMatrixAvoidsLongDirectionalRunsAcrossPhases) {
  auto policy = make_policy();

  for (td::uint64 seed = 1; seed <= 32; seed++) {
    MockRng rng(seed);
    DrsEngine drs(policy, rng);

    std::vector<td::int32> caps;
    caps.reserve(48);
    for (int i = 0; i < 48; i++) {
      auto cap = drs.next_payload_cap(TrafficHint::Interactive);
      caps.push_back(cap);
      drs.notify_bytes_written(static_cast<size_t>(cap));
    }

    ASSERT_TRUE(max_direction_run(caps) <= 6);
  }
}

}  // namespace