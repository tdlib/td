// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/DrsEngine.h"

#include "td/utils/tests.h"

#include <initializer_list>
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

int max_direction_run(const std::vector<td::int32> &caps) {
  CHECK(caps.size() > 2u);
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

TEST(RecordSizeSequenceHardeningAdversarial, AscendingReplayPatternCannotSustainDirectionRuns) {
  DrsPolicy policy;
  policy.slow_start = make_exact_phase({1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700, 1800, 1900, 2000, 2100});
  policy.congestion_open = policy.slow_start;
  policy.steady_state = policy.slow_start;
  policy.slow_start_records = 1024;
  policy.congestion_bytes = 1 << 20;
  policy.idle_reset_ms_min = 1000;
  policy.idle_reset_ms_max = 1000;
  policy.min_payload_cap = 1000;
  policy.max_payload_cap = 2100;

  ScriptedRng rng({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11});
  DrsEngine drs(policy, rng);

  std::vector<td::int32> caps;
  caps.reserve(192);
  for (size_t i = 0; i < 192; i++) {
    caps.push_back(drs.next_payload_cap(TrafficHint::Interactive));
  }

  ASSERT_TRUE(max_direction_run(caps) <= 2);
}

TEST(RecordSizeSequenceHardeningAdversarial, TransitionSmoothingOnlyShapesBoundaryRecord) {
  DrsPolicy policy;
  policy.slow_start = make_exact_phase({1200});
  policy.congestion_open = make_exact_phase({4000});
  policy.steady_state = make_exact_phase({4000});
  policy.slow_start_records = 1;
  policy.congestion_bytes = 1 << 20;
  policy.idle_reset_ms_min = 1000;
  policy.idle_reset_ms_max = 1000;
  policy.min_payload_cap = 1200;
  policy.max_payload_cap = 4000;

  ScriptedRng rng({0, 0, 0, 0, 0, 0});
  DrsEngine drs(policy, rng);

  auto slow_start_cap = drs.next_payload_cap(TrafficHint::Interactive);
  drs.notify_bytes_written(static_cast<size_t>(slow_start_cap));

  auto boundary_cap = drs.next_payload_cap(TrafficHint::Interactive);
  auto second_cap = drs.next_payload_cap(TrafficHint::Interactive);

  ASSERT_TRUE(boundary_cap > slow_start_cap);
  ASSERT_TRUE(boundary_cap < 3600);
  ASSERT_EQ(4000, second_cap);
}

}  // namespace