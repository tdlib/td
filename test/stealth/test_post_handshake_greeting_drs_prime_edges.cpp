// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/DrsEngine.h"

#include "td/utils/tests.h"

#include <vector>

namespace {

using td::mtproto::stealth::DrsEngine;
using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::DrsPolicy;
using td::mtproto::stealth::IRng;
using td::mtproto::stealth::TrafficHint;

class SequenceRng final : public IRng {
 public:
  explicit SequenceRng(std::initializer_list<td::uint32> bounded_values) : bounded_values_(bounded_values) {
    CHECK(!bounded_values_.empty());
  }

  void fill_secure_bytes(td::MutableSlice dest) final {
    dest.fill('\0');
  }

  td::uint32 secure_uint32() final {
    return 0;
  }

  td::uint32 bounded(td::uint32 n) final {
    CHECK(n != 0);
    auto value = bounded_values_[index_ % bounded_values_.size()];
    index_++;
    return value % n;
  }

 private:
  std::vector<td::uint32> bounded_values_;
  size_t index_{0};
};

DrsPhaseModel make_phase(std::initializer_list<td::mtproto::stealth::RecordSizeBin> bins, td::int32 max_repeat_run) {
  DrsPhaseModel phase;
  phase.bins.assign(bins.begin(), bins.end());
  phase.max_repeat_run = max_repeat_run;
  phase.local_jitter = 0;
  return phase;
}

DrsPolicy make_policy(const DrsPhaseModel &slow_start) {
  DrsPolicy policy;
  policy.slow_start = slow_start;
  policy.congestion_open = slow_start;
  policy.steady_state = slow_start;
  policy.slow_start_records = 32;
  policy.congestion_bytes = 1 << 20;
  policy.idle_reset_ms_min = 1000;
  policy.idle_reset_ms_max = 1000;
  policy.min_payload_cap = 900;
  policy.max_payload_cap = 1600;
  return policy;
}

TEST(PostHandshakeGreetingDrsPrimeEdges, PrimedGreetingSizeAvoidsImmediateRepeatWhenAlternativeExists) {
  auto slow_start = make_phase({{1400, 1400, 1}, {1600, 1600, 1}}, 1);
  SequenceRng rng({0, 0, 0, 1, 0});
  DrsEngine engine(make_policy(slow_start), rng);

  engine.prime_with_payload_cap(1400);

  ASSERT_EQ(1600, engine.next_payload_cap(TrafficHint::Interactive));
}

TEST(PostHandshakeGreetingDrsPrimeEdges, PrimedSubMinGreetingSizeDoesNotSuppressFirstSlowStartFloor) {
  auto slow_start = make_phase({{900, 900, 1}, {1200, 1200, 1}}, 1);
  SequenceRng rng({0, 0, 0, 1, 0});
  DrsEngine engine(make_policy(slow_start), rng);

  engine.prime_with_payload_cap(320);

  ASSERT_EQ(900, engine.next_payload_cap(TrafficHint::Interactive));
}

}  // namespace