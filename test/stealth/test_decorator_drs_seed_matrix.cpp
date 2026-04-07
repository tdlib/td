//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/MockClock.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/RecordingTransport.h"

#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"

#include "td/utils/buffer.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <array>
#include <unordered_map>
#include <vector>

namespace {

using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::RecordSizeBin;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;

struct DecoratorFixture final {
  td::unique_ptr<StealthTransportDecorator> decorator;
  RecordingTransport *inner{nullptr};
  MockClock *clock{nullptr};
};

DrsPhaseModel make_phase(std::initializer_list<RecordSizeBin> bins, td::int32 max_repeat_run) {
  DrsPhaseModel phase;
  phase.bins.assign(bins.begin(), bins.end());
  phase.max_repeat_run = max_repeat_run;
  phase.local_jitter = 0;
  return phase;
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

DecoratorFixture make_anti_repeat_fixture(td::uint64 seed) {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_phase({{900, 900, 1}, {1200, 1200, 2}, {1500, 1500, 1}}, 2);
  config.drs_policy.congestion_open = config.drs_policy.slow_start;
  config.drs_policy.steady_state = config.drs_policy.slow_start;
  config.drs_policy.slow_start_records = 1024;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.idle_reset_ms_min = 1000;
  config.drs_policy.idle_reset_ms_max = 1000;
  config.drs_policy.min_payload_cap = 900;
  config.drs_policy.max_payload_cap = 1500;

  auto inner = td::make_unique<RecordingTransport>();
  auto *inner_ptr = inner.get();
  auto clock = td::make_unique<MockClock>();
  auto *clock_ptr = clock.get();
  auto decorator =
      StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(seed), std::move(clock));
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr, clock_ptr};
}

DecoratorFixture make_phase_reset_fixture(td::uint64 seed) {
  MockRng rng(2);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_phase({{900, 900, 1}, {1200, 1200, 1}}, 2);
  config.drs_policy.congestion_open = make_phase({{1800, 1800, 1}}, 2);
  config.drs_policy.steady_state = make_phase({{3200, 3200, 1}, {4800, 4800, 1}}, 2);
  config.drs_policy.slow_start_records = 2;
  config.drs_policy.congestion_bytes = 3000;
  config.drs_policy.idle_reset_ms_min = 100;
  config.drs_policy.idle_reset_ms_max = 100;
  config.drs_policy.min_payload_cap = 900;
  config.drs_policy.max_payload_cap = 4800;

  auto inner = td::make_unique<RecordingTransport>();
  auto *inner_ptr = inner.get();
  auto clock = td::make_unique<MockClock>();
  auto *clock_ptr = clock.get();
  auto decorator =
      StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(seed), std::move(clock));
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr, clock_ptr};
}

td::int32 flush_interactive_and_get_cap(DecoratorFixture &fixture, size_t payload_size) {
  fixture.decorator->set_traffic_hint(TrafficHint::Interactive);
  fixture.decorator->write(
      td::BufferWriter(td::Slice(td::string(payload_size, 'x')), fixture.decorator->max_prepend_size(),
                       fixture.decorator->max_append_size()),
      false);
  fixture.decorator->pre_flush_write(fixture.clock->now());
  CHECK(!fixture.inner->max_tls_record_sizes.empty());
  return fixture.inner->max_tls_record_sizes.back();
}

TEST(DecoratorDrsSeedMatrix, AntiRepeatAndHistogramStayStableAcrossSeededConnections) {
  constexpr std::array<td::uint64, 6> kSeeds = {7, 19, 41, 77, 123, 255};

  std::unordered_map<td::int32, size_t> totals;
  for (auto seed : kSeeds) {
    auto fixture = make_anti_repeat_fixture(seed);
    std::vector<td::int32> caps;
    caps.reserve(48);
    for (size_t i = 0; i < 48; i++) {
      caps.push_back(flush_interactive_and_get_cap(fixture, 64));
      totals[caps.back()]++;
    }

    ASSERT_TRUE(
        std::all_of(caps.begin(), caps.end(), [](td::int32 cap) { return cap == 900 || cap == 1200 || cap == 1500; }));
    ASSERT_TRUE(max_repeat_run(caps) <= 2u);
    ASSERT_TRUE(std::count(caps.begin(), caps.end(), 1200) > 10);
  }

  ASSERT_TRUE(totals[1200] > totals[900]);
  ASSERT_TRUE(totals[1200] > totals[1500]);
  ASSERT_TRUE(totals[1200] > (totals[900] + totals[1200] + totals[1500]) / 3u);
}

TEST(DecoratorDrsSeedMatrix, IdleResetAfterSteadyProgressHoldsAcrossSeededConnections) {
  constexpr std::array<td::uint64, 6> kSeeds = {5, 17, 29, 43, 61, 101};
  for (auto seed : kSeeds) {
    auto fixture = make_phase_reset_fixture(seed);

    auto cap_after_first = flush_interactive_and_get_cap(fixture, 1600);
    ASSERT_TRUE(cap_after_first == 900 || cap_after_first == 1200);

    auto cap_after_second = flush_interactive_and_get_cap(fixture, 1600);
    ASSERT_TRUE(cap_after_second == 900 || cap_after_second == 1200);

    ASSERT_EQ(1800, flush_interactive_and_get_cap(fixture, 1600));
    ASSERT_EQ(1800, flush_interactive_and_get_cap(fixture, 1600));

    auto steady_cap = flush_interactive_and_get_cap(fixture, 1600);
    ASSERT_TRUE(steady_cap == 3200 || steady_cap == 4800);

    fixture.clock->advance(0.2);
    auto reset_cap = flush_interactive_and_get_cap(fixture, 1600);
    ASSERT_TRUE(reset_cap == 900 || reset_cap == 1200);
  }
}

}  // namespace