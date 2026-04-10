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

#include <set>

namespace {

using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::GreetingCamouflagePolicy;
using td::mtproto::stealth::RecordSizeBin;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;

DrsPhaseModel make_phase(std::initializer_list<RecordSizeBin> bins) {
  DrsPhaseModel phase;
  phase.bins.assign(bins.begin(), bins.end());
  phase.max_repeat_run = 8;
  phase.local_jitter = 0;
  return phase;
}

td::BufferWriter make_test_buffer(td::Slice payload) {
  return td::BufferWriter(payload, 32, 0);
}

StealthConfig make_entropy_config() {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_phase({{1400, 1400, 1}});
  config.drs_policy.congestion_open = config.drs_policy.slow_start;
  config.drs_policy.steady_state = config.drs_policy.slow_start;
  config.drs_policy.slow_start_records = 16;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.idle_reset_ms_min = 1000;
  config.drs_policy.idle_reset_ms_max = 1000;
  config.drs_policy.min_payload_cap = 256;
  config.drs_policy.max_payload_cap = 1600;

  GreetingCamouflagePolicy greeting_policy;
  greeting_policy.greeting_record_count = 3;
  greeting_policy.record_models[0] = make_phase({{180, 189, 1}, {210, 219, 1}, {240, 249, 1}});
  greeting_policy.record_models[1] = make_phase({{420, 439, 1}, {520, 539, 1}, {620, 639, 1}});
  greeting_policy.record_models[2] = make_phase({{760, 779, 1}, {860, 879, 1}, {960, 979, 1}});
  greeting_policy.record_models[3] = make_phase({{1100, 1119, 1}});
  greeting_policy.record_models[4] = make_phase({{1300, 1319, 1}});
  config.greeting_camouflage_policy = greeting_policy;
  return config;
}

td::int32 sample_first_record_target(td::uint64 seed) {
  auto inner = td::make_unique<RecordingTransport>();
  inner->supports_tls_record_sizing_result = true;
  auto *inner_ptr = inner.get();
  auto decorator = StealthTransportDecorator::create(std::move(inner), make_entropy_config(),
                                                     td::make_unique<MockRng>(seed), td::make_unique<MockClock>());
  CHECK(decorator.is_ok());

  auto transport = decorator.move_as_ok();
  transport->set_traffic_hint(TrafficHint::Interactive);
  transport->write(make_test_buffer("first"), false);
  transport->pre_flush_write(1000.0);
  auto wakeup = transport->get_shaping_wakeup();
  if (wakeup > 1000.0) {
    transport->pre_flush_write(wakeup);
  }

  CHECK(!inner_ptr->stealth_record_padding_targets.empty());
  return inner_ptr->stealth_record_padding_targets.back();
}

TEST(PostHandshakeGreetingAdversarial, FirstFlightNotAllIdentical) {
  std::set<td::int32> observed;
  for (td::uint64 seed = 1; seed <= 128; seed++) {
    observed.insert(sample_first_record_target(seed));
  }

  ASSERT_TRUE(observed.size() >= 24u);
}

TEST(PostHandshakeGreetingAdversarial, ManualRecordSizeOverrideSuppressesGreetingTemplate) {
  auto inner = td::make_unique<RecordingTransport>();
  inner->supports_tls_record_sizing_result = true;
  auto *inner_ptr = inner.get();
  auto decorator = StealthTransportDecorator::create(std::move(inner), make_entropy_config(),
                                                     td::make_unique<MockRng>(9), td::make_unique<MockClock>());
  ASSERT_TRUE(decorator.is_ok());

  auto transport = decorator.move_as_ok();
  transport->set_max_tls_record_size(1500);
  transport->set_traffic_hint(TrafficHint::Interactive);
  transport->write(make_test_buffer("first"), false);
  transport->pre_flush_write(1000.0);

  ASSERT_FALSE(inner_ptr->stealth_record_padding_targets.empty());
  ASSERT_EQ(1500, inner_ptr->stealth_record_padding_targets.back());
}

TEST(PostHandshakeGreetingAdversarial, GreetingProgressesPerEmittedRecordUnderBackpressure) {
  auto config = make_entropy_config();
  auto inner = td::make_unique<RecordingTransport>();
  inner->supports_tls_record_sizing_result = true;
  inner->writes_per_flush_budget_result = 1;
  auto *inner_ptr = inner.get();

  auto clock = td::make_unique<MockClock>();
  auto *clock_ptr = clock.get();
  auto decorator = StealthTransportDecorator::create(std::move(inner), std::move(config), td::make_unique<MockRng>(13),
                                                     std::move(clock));
  ASSERT_TRUE(decorator.is_ok());
  auto transport = decorator.move_as_ok();

  transport->set_traffic_hint(TrafficHint::Interactive);
  transport->write(make_test_buffer("first"), true);
  transport->set_traffic_hint(TrafficHint::Interactive);
  transport->write(make_test_buffer("second"), false);

  transport->pre_flush_write(clock_ptr->now());
  ASSERT_EQ(1, inner_ptr->write_calls);
  ASSERT_TRUE(inner_ptr->stealth_record_padding_targets.size() >= 1u);
  auto first_target = inner_ptr->stealth_record_padding_targets.back();
  ASSERT_TRUE(first_target >= 180);
  ASSERT_TRUE(first_target <= 249);

  auto wakeup = transport->get_shaping_wakeup();
  ASSERT_TRUE(wakeup >= clock_ptr->now());
  clock_ptr->advance(wakeup - clock_ptr->now());
  transport->pre_flush_write(clock_ptr->now());
  ASSERT_EQ(2, inner_ptr->write_calls);
  ASSERT_TRUE(inner_ptr->stealth_record_padding_targets.size() >= 2u);
  auto second_target = inner_ptr->stealth_record_padding_targets.back();
  ASSERT_TRUE(second_target >= 420);
  ASSERT_TRUE(second_target <= 639);
}

TEST(PostHandshakeGreetingAdversarial, MidFlightManualOverrideStopsFurtherGreetingTemplates) {
  auto inner = td::make_unique<RecordingTransport>();
  inner->supports_tls_record_sizing_result = true;
  auto *inner_ptr = inner.get();
  auto decorator = StealthTransportDecorator::create(std::move(inner), make_entropy_config(),
                                                     td::make_unique<MockRng>(21), td::make_unique<MockClock>());
  ASSERT_TRUE(decorator.is_ok());
  auto transport = decorator.move_as_ok();

  transport->set_traffic_hint(TrafficHint::Interactive);
  transport->write(make_test_buffer("first"), false);
  transport->pre_flush_write(1000.0);
  auto greeting_target = inner_ptr->stealth_record_padding_targets.back();
  ASSERT_TRUE(greeting_target >= 180);
  ASSERT_TRUE(greeting_target <= 249);

  transport->set_max_tls_record_size(1500);
  transport->set_traffic_hint(TrafficHint::Interactive);
  transport->write(make_test_buffer("second"), false);
  transport->pre_flush_write(1000.0);

  ASSERT_EQ(1500, inner_ptr->stealth_record_padding_targets.back());
}

TEST(PostHandshakeGreetingAdversarial, DefaultConfigKeepsGreetingDisabledUntilExplicitlyEnabled) {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_phase({{1400, 1400, 1}});
  config.drs_policy.congestion_open = config.drs_policy.slow_start;
  config.drs_policy.steady_state = config.drs_policy.slow_start;
  config.drs_policy.slow_start_records = 16;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.idle_reset_ms_min = 1000;
  config.drs_policy.idle_reset_ms_max = 1000;
  config.drs_policy.min_payload_cap = 256;
  config.drs_policy.max_payload_cap = 1600;

  auto inner = td::make_unique<RecordingTransport>();
  inner->supports_tls_record_sizing_result = true;
  auto *inner_ptr = inner.get();
  auto decorator = StealthTransportDecorator::create(std::move(inner), std::move(config), td::make_unique<MockRng>(23),
                                                     td::make_unique<MockClock>());
  ASSERT_TRUE(decorator.is_ok());

  auto transport = decorator.move_as_ok();
  transport->set_traffic_hint(TrafficHint::Interactive);
  transport->write(make_test_buffer("first"), false);
  transport->pre_flush_write(1000.0);

  ASSERT_FALSE(inner_ptr->stealth_record_padding_targets.empty());
  ASSERT_EQ(1400, inner_ptr->stealth_record_padding_targets.back());
}

}  // namespace