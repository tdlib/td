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

#include <set>
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

enum class Bucket : td::uint8 { Low = 0, Mid = 1, High = 2 };

DrsPhaseModel make_phase(std::initializer_list<RecordSizeBin> bins, td::int32 local_jitter) {
  DrsPhaseModel phase;
  phase.bins.assign(bins.begin(), bins.end());
  phase.max_repeat_run = 8;
  phase.local_jitter = local_jitter;
  return phase;
}

StealthConfig make_distribution_config() {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_phase({{900, 908, 1}, {1200, 1208, 6}, {1500, 1508, 1}}, 3);
  config.drs_policy.congestion_open = config.drs_policy.slow_start;
  config.drs_policy.steady_state = config.drs_policy.slow_start;
  config.drs_policy.slow_start_records = 1024;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.idle_reset_ms_min = 1000;
  config.drs_policy.idle_reset_ms_max = 1000;
  config.drs_policy.min_payload_cap = 900;
  config.drs_policy.max_payload_cap = 1508;
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

Bucket classify_payload_cap(td::int32 cap) {
  if (cap >= 900 && cap <= 908) {
    return Bucket::Low;
  }
  if (cap >= 1200 && cap <= 1208) {
    return Bucket::Mid;
  }
  if (cap >= 1500 && cap <= 1508) {
    return Bucket::High;
  }
  LOG(FATAL) << "Unexpected payload cap " << cap;
  UNREACHABLE();
}

TEST(DecoratorDrsJitterDistribution, InteractiveCapsStayWithinJitteredBinsWithoutModeCollapse) {
  std::unordered_map<Bucket, size_t> bucket_counts;
  std::unordered_map<Bucket, std::set<td::int32>> bucket_values;
  for (size_t seed = 1; seed <= kSampleCount; seed++) {
    auto cap = sample_first_interactive_cap(static_cast<td::uint64>(seed));
    auto bucket = classify_payload_cap(cap);
    bucket_counts[bucket]++;
    bucket_values[bucket].insert(cap);
  }

  ASSERT_EQ(kSampleCount, bucket_counts[Bucket::Low] + bucket_counts[Bucket::Mid] + bucket_counts[Bucket::High]);
  ASSERT_TRUE(bucket_counts[Bucket::Low] > 0u);
  ASSERT_TRUE(bucket_counts[Bucket::Mid] > 0u);
  ASSERT_TRUE(bucket_counts[Bucket::High] > 0u);
  ASSERT_TRUE(bucket_counts[Bucket::Mid] > bucket_counts[Bucket::Low] * 2u);
  ASSERT_TRUE(bucket_counts[Bucket::Mid] > bucket_counts[Bucket::High] * 2u);
  ASSERT_TRUE(bucket_counts[Bucket::Mid] > kSampleCount / 2u);
  ASSERT_TRUE(bucket_values[Bucket::Low].size() >= 4u);
  ASSERT_TRUE(bucket_values[Bucket::Mid].size() >= 6u);
  ASSERT_TRUE(bucket_values[Bucket::High].size() >= 4u);
}

}  // namespace