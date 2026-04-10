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

DrsPolicy make_policy() {
  DrsPolicy policy;
  policy.slow_start = make_exact_phase({1000, 1200});
  policy.congestion_open = policy.slow_start;
  policy.steady_state = policy.slow_start;
  policy.slow_start_records = 1024;
  policy.congestion_bytes = 1 << 20;
  policy.idle_reset_ms_min = 1000;
  policy.idle_reset_ms_max = 1000;
  policy.min_payload_cap = 900;
  policy.max_payload_cap = 1200;
  return policy;
}

ScriptedRng make_sequence_rng() {
  return ScriptedRng({0, 0, 0, 1});
}

TEST(RecordSizeSequenceHardeningStateEdges, ControlHintsDoNotClearInteractiveAntiRepeatHistory) {
  auto policy = make_policy();
  auto rng = make_sequence_rng();
  DrsEngine drs(policy, rng);

  ASSERT_EQ(1000, drs.next_payload_cap(TrafficHint::Interactive));
  ASSERT_EQ(policy.min_payload_cap, drs.next_payload_cap(TrafficHint::Keepalive));
  ASSERT_EQ(policy.min_payload_cap, drs.next_payload_cap(TrafficHint::AuthHandshake));
  ASSERT_EQ(1200, drs.next_payload_cap(TrafficHint::Interactive));
}

TEST(RecordSizeSequenceHardeningStateEdges, NotifyIdleClearsInteractiveSequenceHistory) {
  auto policy = make_policy();
  auto rng = make_sequence_rng();
  DrsEngine drs(policy, rng);

  ASSERT_EQ(1000, drs.next_payload_cap(TrafficHint::Interactive));
  drs.notify_idle();
  ASSERT_EQ(1000, drs.next_payload_cap(TrafficHint::Interactive));
}

TEST(RecordSizeSequenceHardeningStateEdges, PrimedGreetingHistoryFeedsFirstDrsSample) {
  auto policy = make_policy();
  auto rng = make_sequence_rng();
  DrsEngine drs(policy, rng);

  drs.prime_with_payload_cap(1000);
  ASSERT_EQ(1200, drs.next_payload_cap(TrafficHint::Interactive));
}

}  // namespace