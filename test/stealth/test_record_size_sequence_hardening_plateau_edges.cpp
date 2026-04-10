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
  explicit ScriptedRng(std::vector<td::uint32> selections) : selections_(std::move(selections)) {
    CHECK(!selections_.empty());
  }

  void fill_secure_bytes(td::MutableSlice dest) final {
    for (size_t index = 0; index < dest.size(); index++) {
      dest[index] = static_cast<char>(next_selection() & 0xffu);
    }
  }

  td::uint32 secure_uint32() final {
    return next_selection();
  }

  td::uint32 bounded(td::uint32 n) final {
    CHECK(n != 0u);
    if (n == 1u) {
      return 0u;
    }
    return next_selection() % n;
  }

 private:
  std::vector<td::uint32> selections_;
  size_t selection_index_{0};

  td::uint32 next_selection() {
    auto value = selections_[selection_index_ % selections_.size()];
    selection_index_++;
    return value;
  }
};

DrsPhaseModel make_exact_phase(std::initializer_list<td::int32> caps) {
  DrsPhaseModel phase;
  phase.max_repeat_run = 2;
  phase.local_jitter = 0;
  for (auto cap : caps) {
    phase.bins.push_back(RecordSizeBin{cap, cap, 1});
  }
  return phase;
}

DrsPolicy make_policy() {
  DrsPolicy policy;
  policy.slow_start = make_exact_phase({1000, 1100, 1200, 1300});
  policy.congestion_open = policy.slow_start;
  policy.steady_state = policy.slow_start;
  policy.slow_start_records = 1024;
  policy.congestion_bytes = 1 << 20;
  policy.idle_reset_ms_min = 1000;
  policy.idle_reset_ms_max = 1000;
  policy.min_payload_cap = 1000;
  policy.max_payload_cap = 1300;
  return policy;
}

TEST(RecordSizeSequenceHardeningPlateauEdges, AscendingPlateauResetsDirectionalPressure) {
  auto policy = make_policy();
  std::vector<td::uint32> selections;
  selections.insert(selections.end(), 32, 0);
  selections.insert(selections.end(), 32, 1);
  selections.insert(selections.end(), 32, 2);
  selections.insert(selections.end(), 32, 2);
  selections.push_back(3);
  selections.insert(selections.end(), 31, 0);
  ScriptedRng rng(std::move(selections));
  DrsEngine drs(policy, rng);

  ASSERT_EQ(1000, drs.next_payload_cap(TrafficHint::Interactive));
  ASSERT_EQ(1100, drs.next_payload_cap(TrafficHint::Interactive));
  ASSERT_EQ(1200, drs.next_payload_cap(TrafficHint::Interactive));
  ASSERT_EQ(1200, drs.next_payload_cap(TrafficHint::Interactive));
  ASSERT_EQ(1300, drs.next_payload_cap(TrafficHint::Interactive));
}

TEST(RecordSizeSequenceHardeningPlateauEdges, DescendingPlateauResetsDirectionalPressure) {
  auto policy = make_policy();
  std::vector<td::uint32> selections;
  selections.insert(selections.end(), 32, 3);
  selections.insert(selections.end(), 32, 2);
  selections.insert(selections.end(), 32, 1);
  selections.insert(selections.end(), 32, 1);
  selections.push_back(0);
  selections.insert(selections.end(), 31, 3);
  ScriptedRng rng(std::move(selections));
  DrsEngine drs(policy, rng);

  ASSERT_EQ(1300, drs.next_payload_cap(TrafficHint::Interactive));
  ASSERT_EQ(1200, drs.next_payload_cap(TrafficHint::Interactive));
  ASSERT_EQ(1100, drs.next_payload_cap(TrafficHint::Interactive));
  ASSERT_EQ(1100, drs.next_payload_cap(TrafficHint::Interactive));
  ASSERT_EQ(1000, drs.next_payload_cap(TrafficHint::Interactive));
}

}  // namespace