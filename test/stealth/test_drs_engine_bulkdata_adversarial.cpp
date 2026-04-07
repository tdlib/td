//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
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
  policy.steady_state = make_phase({{3000, 3000, 1}});
  policy.slow_start_records = 2;
  policy.congestion_bytes = 4096;
  policy.idle_reset_ms_min = 100;
  policy.idle_reset_ms_max = 100;
  policy.min_payload_cap = 900;
  policy.max_payload_cap = 3000;
  return policy;
}

TEST(DrsEngineBulkDataAdversarial, BulkDataUsesSteadyStateCapBeforePhasePromotion) {
  MockRng rng(13);
  auto policy = make_policy();
  DrsEngine drs(policy, rng);

  ASSERT_EQ(3000, drs.next_payload_cap(TrafficHint::BulkData));
  ASSERT_EQ(900, drs.next_payload_cap(TrafficHint::Interactive));
}

}  // namespace