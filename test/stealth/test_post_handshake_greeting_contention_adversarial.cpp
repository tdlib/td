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

#include <cmath>

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

td::BufferWriter make_test_buffer(size_t size) {
  return td::BufferWriter(td::Slice(td::string(size, 'x')), 32, 0);
}

StealthConfig make_contention_config() {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_exact_phase(1400);
  config.drs_policy.congestion_open = make_exact_phase(1800);
  config.drs_policy.steady_state = make_exact_phase(2400);
  config.drs_policy.slow_start_records = 16;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.min_payload_cap = 256;
  config.drs_policy.max_payload_cap = 2400;

  config.ipt_params.burst_mu_ms = std::log(50.0);
  config.ipt_params.burst_sigma = 0.0;
  config.ipt_params.burst_max_ms = 50.0;
  config.ipt_params.idle_alpha = 2.0;
  config.ipt_params.idle_scale_ms = 10.0;
  config.ipt_params.idle_max_ms = 100.0;
  config.ipt_params.p_burst_stay = 1.0;
  config.ipt_params.p_idle_to_burst = 1.0;

  GreetingCamouflagePolicy greeting_policy;
  greeting_policy.greeting_record_count = 3;
  greeting_policy.record_models[0] = make_exact_phase(320);
  greeting_policy.record_models[1] = make_exact_phase(640);
  greeting_policy.record_models[2] = make_exact_phase(960);
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

  auto decorator = StealthTransportDecorator::create(std::move(inner), make_contention_config(),
                                                     td::make_unique<MockRng>(7), std::move(clock));
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr, clock_ptr};
}

void enqueue_packet(DecoratorFixture &fixture, size_t payload_size, TrafficHint hint, bool quick_ack) {
  fixture.decorator->set_traffic_hint(hint);
  fixture.decorator->write(make_test_buffer(payload_size), quick_ack);
}

double leave_delayed_interactive_queued(DecoratorFixture &fixture, size_t immediate_payload_size = 19,
                                        size_t delayed_payload_size = 21, int write_budget = 64) {
  auto previous_budget = fixture.inner->writes_per_flush_budget_result;
  enqueue_packet(fixture, immediate_payload_size, TrafficHint::Interactive, false);
  enqueue_packet(fixture, delayed_payload_size, TrafficHint::Interactive, false);
  fixture.inner->writes_per_flush_budget_result = write_budget;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  fixture.inner->writes_per_flush_budget_result = previous_budget;
  auto wakeup = fixture.decorator->get_shaping_wakeup();
  ASSERT_TRUE(wakeup > fixture.clock->now());
  return wakeup;
}

TEST(PostHandshakeGreetingContentionAdversarial,
     GreetingSlotsAdvanceByEmittedOrderAcrossAlternatingBypassAndShapedContention) {
  auto fixture = make_fixture();

  auto wakeup = leave_delayed_interactive_queued(fixture);
  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_FALSE(fixture.inner->stealth_record_padding_targets.empty());
  ASSERT_EQ(320, fixture.inner->stealth_record_padding_targets.back());

  enqueue_packet(fixture, 41, TrafficHint::Keepalive, true);
  enqueue_packet(fixture, 43, TrafficHint::Keepalive, true);

  fixture.clock->advance(wakeup - fixture.clock->now());

  fixture.inner->writes_per_flush_budget_result = 1;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(TrafficHint::Keepalive, fixture.inner->queued_hints.back());
  ASSERT_EQ(640, fixture.inner->stealth_record_padding_targets.back());

  fixture.inner->writes_per_flush_budget_result = 1;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(3, fixture.inner->write_calls);
  ASSERT_EQ(TrafficHint::Interactive, fixture.inner->queued_hints.back());
  ASSERT_EQ(td::string(21, 'x'), fixture.inner->written_payloads.back());
  ASSERT_EQ(960, fixture.inner->stealth_record_padding_targets.back());

  fixture.inner->writes_per_flush_budget_result = 1;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(4, fixture.inner->write_calls);
  ASSERT_EQ(TrafficHint::Keepalive, fixture.inner->queued_hints.back());
  ASSERT_EQ(256, fixture.inner->stealth_record_padding_targets.back());
}

TEST(PostHandshakeGreetingContentionAdversarial, BlockedMixedRingContentionDoesNotConsumeGreetingSlotsBeforeFirstEmit) {
  auto fixture = make_fixture();

  auto wakeup = leave_delayed_interactive_queued(fixture);
  enqueue_packet(fixture, 41, TrafficHint::Keepalive, true);

  fixture.clock->advance(wakeup - fixture.clock->now());
  auto target_count_before_block = fixture.inner->stealth_record_padding_targets.size();
  fixture.inner->can_write_result = false;
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(target_count_before_block, fixture.inner->stealth_record_padding_targets.size());

  fixture.inner->can_write_result = true;
  fixture.inner->writes_per_flush_budget_result = 1;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(640, fixture.inner->stealth_record_padding_targets.back());

  fixture.inner->writes_per_flush_budget_result = 1;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(3, fixture.inner->write_calls);
  ASSERT_EQ(960, fixture.inner->stealth_record_padding_targets.back());
}

TEST(PostHandshakeGreetingContentionAdversarial,
     ManualOverrideSuppressesGreetingTemplatesAcrossBothRingsDuringContention) {
  auto fixture = make_fixture();

  auto wakeup = leave_delayed_interactive_queued(fixture);
  enqueue_packet(fixture, 41, TrafficHint::Keepalive, true);
  enqueue_packet(fixture, 43, TrafficHint::Keepalive, true);

  fixture.decorator->set_max_tls_record_size(1500);
  fixture.clock->advance(wakeup - fixture.clock->now());

  fixture.inner->writes_per_flush_budget_result = 1;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(1500, fixture.inner->stealth_record_padding_targets.back());

  fixture.inner->writes_per_flush_budget_result = 1;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(3, fixture.inner->write_calls);
  ASSERT_EQ(1500, fixture.inner->stealth_record_padding_targets.back());

  fixture.inner->writes_per_flush_budget_result = 1;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(4, fixture.inner->write_calls);
  ASSERT_EQ(1500, fixture.inner->stealth_record_padding_targets.back());
}

}  // namespace