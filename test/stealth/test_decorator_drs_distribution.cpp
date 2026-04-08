// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockClock.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/RecordingTransport.h"

#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"

#include "td/utils/buffer.h"
#include "td/utils/tests.h"

#include <unordered_map>

namespace {

using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::RecordSizeBin;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;

constexpr size_t kSampleCount = 256;

DrsPhaseModel make_phase(std::initializer_list<RecordSizeBin> bins) {
  DrsPhaseModel phase;
  phase.bins.assign(bins.begin(), bins.end());
  phase.max_repeat_run = 8;
  phase.local_jitter = 0;
  return phase;
}

StealthConfig make_distribution_config() {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_phase({{900, 900, 1}, {1200, 1200, 6}, {1500, 1500, 1}});
  config.drs_policy.congestion_open = config.drs_policy.slow_start;
  config.drs_policy.steady_state = config.drs_policy.slow_start;
  config.drs_policy.slow_start_records = 1024;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.idle_reset_ms_min = 1000;
  config.drs_policy.idle_reset_ms_max = 1000;
  config.drs_policy.min_payload_cap = 900;
  config.drs_policy.max_payload_cap = 1500;
  return config;
}

td::int32 sample_first_interactive_cap(td::uint64 seed) {
  auto inner = td::make_unique<RecordingTransport>();
  auto *inner_ptr = inner.get();
  auto decorator = StealthTransportDecorator::create(std::move(inner), make_distribution_config(),
                                                     td::make_unique<MockRng>(seed), td::make_unique<MockClock>());
  CHECK(decorator.is_ok());

  auto transport = decorator.move_as_ok();
  transport->set_traffic_hint(TrafficHint::Interactive);
  transport->write(td::BufferWriter(td::Slice("payload"), transport->max_prepend_size(), transport->max_append_size()),
                   false);
  transport->pre_flush_write(1000.0);

  CHECK(!inner_ptr->max_tls_record_sizes.empty());
  return inner_ptr->max_tls_record_sizes.back();
}

TEST(DecoratorDrsDistribution, InteractiveFlushesPreserveWeightedCaptureLikeBins) {
  std::unordered_map<td::int32, size_t> counts;
  for (size_t seed = 1; seed <= kSampleCount; seed++) {
    counts[sample_first_interactive_cap(static_cast<td::uint64>(seed))]++;
  }

  ASSERT_EQ(3u, counts.size());
  ASSERT_TRUE(counts[900] > 0u);
  ASSERT_TRUE(counts[1200] > 0u);
  ASSERT_TRUE(counts[1500] > 0u);
  ASSERT_TRUE(counts[1200] > counts[900] * 2u);
  ASSERT_TRUE(counts[1200] > counts[1500] * 2u);
  ASSERT_TRUE(counts[1200] > kSampleCount / 2u);
}

}  // namespace