// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/DrsEngine.h"
#include "td/mtproto/stealth/StealthConfig.h"

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

int phase_value(DrsEngine::Phase phase) {
  return static_cast<int>(phase);
}

DrsPhaseModel make_phase(std::initializer_list<RecordSizeBin> bins, td::int32 max_repeat_run, td::int32 local_jitter) {
  DrsPhaseModel phase;
  phase.bins.assign(bins.begin(), bins.end());
  phase.max_repeat_run = max_repeat_run;
  phase.local_jitter = local_jitter;
  return phase;
}

DrsPolicy make_test_policy() {
  DrsPolicy policy;
  policy.slow_start = make_phase({{900, 900, 1}, {1200, 1200, 2}}, 2, 0);
  policy.congestion_open = make_phase({{1400, 1400, 1}, {1800, 1800, 1}}, 2, 0);
  policy.steady_state = make_phase({{3200, 3200, 1}, {4800, 4800, 2}}, 2, 0);
  policy.slow_start_records = 2;
  policy.congestion_bytes = 2600;
  policy.idle_reset_ms_min = 250;
  policy.idle_reset_ms_max = 1200;
  policy.min_payload_cap = 900;
  policy.max_payload_cap = 6000;
  return policy;
}

size_t max_repeat_run(const std::vector<td::int32> &series) {
  if (series.empty()) {
    return 0;
  }
  size_t best = 1;
  size_t current = 1;
  for (size_t i = 1; i < series.size(); i++) {
    if (series[i] == series[i - 1]) {
      current++;
      best = std::max(best, current);
    } else {
      current = 1;
    }
  }
  return best;
}

bool is_one_of(td::int32 value, std::initializer_list<td::int32> expected) {
  return std::find(expected.begin(), expected.end(), value) != expected.end();
}

TEST(DrsEngine, AdvancesPhaseByRealWrittenBytes) {
  MockRng rng(1);
  auto policy = make_test_policy();
  DrsEngine drs(policy, rng);

  ASSERT_EQ(phase_value(drs.current_phase()), phase_value(DrsEngine::Phase::SlowStart));

  for (int i = 0; i < policy.slow_start_records; i++) {
    ASSERT_TRUE(is_one_of(drs.next_payload_cap(TrafficHint::Interactive), {900, 1200}));
    drs.notify_bytes_written(1300);
  }
  ASSERT_EQ(phase_value(drs.current_phase()), phase_value(DrsEngine::Phase::CongestionOpen));

  ASSERT_TRUE(is_one_of(drs.next_payload_cap(TrafficHint::Interactive), {1400, 1800}));
  drs.notify_bytes_written(policy.congestion_bytes);
  ASSERT_EQ(phase_value(drs.current_phase()), phase_value(DrsEngine::Phase::SteadyState));
  ASSERT_TRUE(is_one_of(drs.next_payload_cap(TrafficHint::Interactive), {3200, 4800}));
}

TEST(DrsEngine, IdleResetThresholdIsSampledPerConnection) {
  auto policy = make_test_policy();
  MockRng rng_a(7);
  MockRng rng_b(8);
  DrsEngine a(policy, rng_a);
  DrsEngine b(policy, rng_b);

  ASSERT_NE(a.debug_idle_reset_ms_for_tests(), b.debug_idle_reset_ms_for_tests());
}

TEST(DrsEngine, NotifyIdleRestartsSlowStartAfterProgress) {
  MockRng rng(9);
  auto policy = make_test_policy();
  DrsEngine drs(policy, rng);

  for (int i = 0; i < policy.slow_start_records; i++) {
    drs.next_payload_cap(TrafficHint::Interactive);
    drs.notify_bytes_written(1300);
  }
  ASSERT_EQ(phase_value(drs.current_phase()), phase_value(DrsEngine::Phase::CongestionOpen));

  drs.notify_idle();
  ASSERT_EQ(phase_value(drs.current_phase()), phase_value(DrsEngine::Phase::SlowStart));
  ASSERT_TRUE(is_one_of(drs.next_payload_cap(TrafficHint::Interactive), {900, 1200}));
}

TEST(DrsEngine, DistributionUsesAntiRepeatGuard) {
  MockRng rng(42);
  auto policy = make_test_policy();
  policy.slow_start_records = 1000;
  DrsEngine drs(policy, rng);

  std::vector<td::int32> series;
  for (int i = 0; i < 128; i++) {
    series.push_back(drs.next_payload_cap(TrafficHint::Interactive));
  }

  std::sort(series.begin(), series.end());
  auto unique_end = std::unique(series.begin(), series.end());
  ASSERT_TRUE(static_cast<size_t>(unique_end - series.begin()) > 1u);

  series.clear();
  for (int i = 0; i < 128; i++) {
    series.push_back(drs.next_payload_cap(TrafficHint::Interactive));
  }
  ASSERT_TRUE(max_repeat_run(series) <= static_cast<size_t>(policy.slow_start.max_repeat_run));
}

}  // namespace