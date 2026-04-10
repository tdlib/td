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

td::BufferWriter make_test_buffer(td::Slice payload) {
  return td::BufferWriter(payload, 32, 0);
}

StealthConfig make_transition_config() {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_exact_phase(1400);
  config.drs_policy.congestion_open = make_exact_phase(1800);
  config.drs_policy.steady_state = make_exact_phase(2400);
  config.drs_policy.slow_start_records = 16;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.idle_reset_ms_min = 1000;
  config.drs_policy.idle_reset_ms_max = 1000;
  config.drs_policy.min_payload_cap = 256;
  config.drs_policy.max_payload_cap = 2400;

  GreetingCamouflagePolicy greeting_policy;
  greeting_policy.greeting_record_count = 1;
  greeting_policy.record_models[0] = make_exact_phase(320);
  config.greeting_camouflage_policy = greeting_policy;
  return config;
}

struct DecoratorFixture final {
  td::unique_ptr<StealthTransportDecorator> decorator;
  RecordingTransport *inner{nullptr};
  MockClock *clock{nullptr};
};

DecoratorFixture make_fixture() {
  auto inner = td::make_unique<RecordingTransport>();
  inner->supports_tls_record_sizing_result = true;
  auto *inner_ptr = inner.get();

  auto clock = td::make_unique<MockClock>();
  auto *clock_ptr = clock.get();

  auto decorator = StealthTransportDecorator::create(std::move(inner), make_transition_config(),
                                                     td::make_unique<MockRng>(7), std::move(clock));
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr, clock_ptr};
}

TEST(PostHandshakeGreetingTransitionEdges, GreetingExhaustionSwitchesToDrsWithinSameFlushCycle) {
  auto fixture = make_fixture();

  fixture.decorator->set_traffic_hint(TrafficHint::Interactive);
  fixture.decorator->write(make_test_buffer("first"), true);
  fixture.decorator->set_traffic_hint(TrafficHint::Interactive);
  fixture.decorator->write(make_test_buffer("second"), false);

  fixture.decorator->pre_flush_write(fixture.clock->now());
  auto wakeup = fixture.decorator->get_shaping_wakeup();
  if (wakeup > fixture.clock->now()) {
    fixture.clock->advance(wakeup - fixture.clock->now());
    fixture.decorator->pre_flush_write(fixture.clock->now());
  }

  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_TRUE(fixture.inner->stealth_record_padding_targets.size() >= 2u);
  ASSERT_EQ(320, fixture.inner->stealth_record_padding_targets.front());
  ASSERT_EQ(1400, fixture.inner->stealth_record_padding_targets.back());
}

}  // namespace