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

DrsPhaseModel make_phase(std::initializer_list<RecordSizeBin> bins, td::int32 max_repeat_run = 1) {
  DrsPhaseModel phase;
  phase.bins.assign(bins.begin(), bins.end());
  phase.max_repeat_run = max_repeat_run;
  phase.local_jitter = 0;
  return phase;
}

DrsPolicy make_policy() {
  DrsPolicy policy;
  policy.slow_start = make_phase({{900, 900, 1}, {1200, 1200, 1}}, 2);
  policy.congestion_open = make_phase({{1400, 1400, 1}});
  policy.steady_state = make_phase({{4000, 4000, 1}});
  policy.slow_start_records = 2;
  policy.congestion_bytes = 2048;
  policy.idle_reset_ms_min = 100;
  policy.idle_reset_ms_max = 200;
  policy.min_payload_cap = 900;
  policy.max_payload_cap = 4096;
  return policy;
}

TEST(DrsEngineAdversarial, KeepaliveAndAuthHintsClampToMinimumCap) {
  MockRng rng(11);
  auto policy = make_policy();
  DrsEngine drs(policy, rng);

  ASSERT_EQ(policy.min_payload_cap, drs.next_payload_cap(TrafficHint::Keepalive));
  ASSERT_EQ(policy.min_payload_cap, drs.next_payload_cap(TrafficHint::AuthHandshake));
}

TEST(DrsEngineAdversarial, SingleValuePhaseRemainsStableUnderRepeatGuard) {
  MockRng rng(12);
  auto policy = make_policy();
  policy.slow_start = make_phase({{1337, 1337, 1}}, 1);
  DrsEngine drs(policy, rng);

  for (int i = 0; i < 16; i++) {
    ASSERT_EQ(1337, drs.next_payload_cap(TrafficHint::Interactive));
  }
}

}  // namespace