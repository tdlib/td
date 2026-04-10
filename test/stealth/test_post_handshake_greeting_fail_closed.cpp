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

namespace {

using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::GreetingCamouflagePolicy;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;

DrsPhaseModel make_exact_phase(td::int32 record_size) {
  DrsPhaseModel phase;
  phase.bins = {{record_size, record_size, 1}};
  phase.max_repeat_run = 1;
  phase.local_jitter = 0;
  return phase;
}

StealthConfig make_greeting_config() {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_exact_phase(1400);
  config.drs_policy.congestion_open = make_exact_phase(1400);
  config.drs_policy.steady_state = make_exact_phase(1400);
  config.drs_policy.slow_start_records = 16;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.idle_reset_ms_min = 1000;
  config.drs_policy.idle_reset_ms_max = 1000;
  config.drs_policy.min_payload_cap = 256;
  config.drs_policy.max_payload_cap = 1600;

  GreetingCamouflagePolicy greeting_policy;
  greeting_policy.greeting_record_count = 3;
  greeting_policy.record_models[0] = make_exact_phase(320);
  greeting_policy.record_models[1] = make_exact_phase(640);
  greeting_policy.record_models[2] = make_exact_phase(960);
  config.greeting_camouflage_policy = greeting_policy;
  return config;
}

td::BufferWriter make_test_buffer(td::Slice payload) {
  return td::BufferWriter(payload, 32, 0);
}

TEST(PostHandshakeGreetingFailClosed, GreetingRequiresTlsRecordSizingSupport) {
  auto inner = td::make_unique<RecordingTransport>();
  inner->supports_tls_record_sizing_result = false;

  auto decorator = StealthTransportDecorator::create(std::move(inner), make_greeting_config(),
                                                     td::make_unique<MockRng>(5), td::make_unique<MockClock>());

  ASSERT_TRUE(decorator.is_error());
}

TEST(PostHandshakeGreetingFailClosed, DrsOnlyConfigCanRunWithoutTlsRecordSizingSupport) {
  auto config = make_greeting_config();
  config.greeting_camouflage_policy.greeting_record_count = 0;

  auto inner = td::make_unique<RecordingTransport>();
  inner->supports_tls_record_sizing_result = false;

  auto decorator = StealthTransportDecorator::create(std::move(inner), std::move(config), td::make_unique<MockRng>(7),
                                                     td::make_unique<MockClock>());

  ASSERT_TRUE(decorator.is_ok());
}

TEST(PostHandshakeGreetingFailClosed, BlockedFlushDoesNotConsumeGreetingTemplateSlot) {
  auto inner = td::make_unique<RecordingTransport>();
  inner->supports_tls_record_sizing_result = true;
  inner->can_write_result = false;
  auto *inner_ptr = inner.get();

  auto clock = td::make_unique<MockClock>();
  auto *clock_ptr = clock.get();
  auto decorator = StealthTransportDecorator::create(std::move(inner), make_greeting_config(),
                                                     td::make_unique<MockRng>(11), std::move(clock));
  ASSERT_TRUE(decorator.is_ok());

  auto transport = decorator.move_as_ok();
  transport->set_traffic_hint(TrafficHint::Interactive);
  transport->write(make_test_buffer("first"), false);
  transport->pre_flush_write(clock_ptr->now());

  ASSERT_EQ(0, inner_ptr->write_calls);
  ASSERT_TRUE(inner_ptr->stealth_record_padding_targets.empty());

  inner_ptr->can_write_result = true;
  transport->pre_flush_write(clock_ptr->now());
  auto wakeup = transport->get_shaping_wakeup();
  if (wakeup > clock_ptr->now()) {
    clock_ptr->advance(wakeup - clock_ptr->now());
    transport->pre_flush_write(clock_ptr->now());
  }

  ASSERT_EQ(1, inner_ptr->write_calls);
  ASSERT_FALSE(inner_ptr->stealth_record_padding_targets.empty());
  ASSERT_EQ(320, inner_ptr->stealth_record_padding_targets.back());
}

}  // namespace