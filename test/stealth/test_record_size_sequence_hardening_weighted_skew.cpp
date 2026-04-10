// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/DrsEngine.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::DrsEngine;
using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::DrsPolicy;
using td::mtproto::stealth::IRng;
using td::mtproto::stealth::RecordSizeBin;
using td::mtproto::stealth::TrafficHint;

class DominantBinRng final : public IRng {
 public:
  void fill_secure_bytes(td::MutableSlice dest) final {
    dest.fill('\0');
  }

  td::uint32 secure_uint32() final {
    return 0u;
  }

  td::uint32 bounded(td::uint32 n) final {
    CHECK(n != 0u);
    return 0u;
  }
};

DrsPolicy make_policy() {
  DrsPhaseModel phase;
  phase.bins.push_back(RecordSizeBin{1000, 1000, 100});
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

TEST(RecordSizeSequenceHardeningWeightedSkew, RepeatGuardEscapesDominantBinWhenAlternateCapExists) {
  auto policy = make_policy();
  DominantBinRng rng;
  DrsEngine drs(policy, rng);

  ASSERT_EQ(1000, drs.next_payload_cap(TrafficHint::Interactive));
  ASSERT_EQ(1000, drs.next_payload_cap(TrafficHint::Interactive));
  ASSERT_EQ(1400, drs.next_payload_cap(TrafficHint::Interactive));
}

}  // namespace