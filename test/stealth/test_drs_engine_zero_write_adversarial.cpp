// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/DrsEngine.h"

#include "td/utils/tests.h"

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

DrsPhaseModel make_phase(std::initializer_list<RecordSizeBin> bins) {
  DrsPhaseModel phase;
  phase.bins.assign(bins.begin(), bins.end());
  phase.max_repeat_run = 1;
  phase.local_jitter = 0;
  return phase;
}

DrsPolicy make_policy() {
  DrsPolicy policy;
  policy.slow_start = make_phase({{900, 900, 1}});
  policy.congestion_open = make_phase({{1400, 1400, 1}});
  policy.steady_state = make_phase({{2400, 2400, 1}});
  policy.slow_start_records = 2;
  policy.congestion_bytes = 2800;
  policy.idle_reset_ms_min = 100;
  policy.idle_reset_ms_max = 100;
  policy.min_payload_cap = 900;
  policy.max_payload_cap = 2400;
  return policy;
}

TEST(DrsEngineZeroWriteAdversarial, ZeroByteNotificationsDoNotAdvancePhaseCounters) {
  MockRng rng(51);
  auto policy = make_policy();
  DrsEngine drs(policy, rng);

  drs.notify_bytes_written(0);
  drs.notify_bytes_written(0);
  ASSERT_EQ(phase_value(DrsEngine::Phase::SlowStart), phase_value(drs.current_phase()));
  ASSERT_EQ(900, drs.next_payload_cap(TrafficHint::Interactive));

  drs.notify_bytes_written(900);
  ASSERT_EQ(phase_value(DrsEngine::Phase::SlowStart), phase_value(drs.current_phase()));
  ASSERT_EQ(900, drs.next_payload_cap(TrafficHint::Interactive));

  drs.notify_bytes_written(900);
  ASSERT_EQ(phase_value(DrsEngine::Phase::CongestionOpen), phase_value(drs.current_phase()));
  ASSERT_EQ(1400, drs.next_payload_cap(TrafficHint::Interactive));
}

}  // namespace