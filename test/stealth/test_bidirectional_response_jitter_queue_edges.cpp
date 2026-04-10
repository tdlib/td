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
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;

DrsPhaseModel make_fixed_phase(td::int32 cap) {
  DrsPhaseModel phase;
  phase.bins = {{cap, cap, 1}};
  phase.max_repeat_run = 16;
  phase.local_jitter = 0;
  return phase;
}

StealthConfig make_jitter_config() {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_fixed_phase(512);
  config.drs_policy.congestion_open = make_fixed_phase(512);
  config.drs_policy.steady_state = make_fixed_phase(512);
  config.drs_policy.slow_start_records = 1024;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.min_payload_cap = 512;
  config.drs_policy.max_payload_cap = 512;
  config.ipt_params.burst_mu_ms = 0.0;
  config.ipt_params.burst_sigma = 0.0;
  config.ipt_params.burst_max_ms = 1.0;
  config.ipt_params.idle_alpha = 1.0;
  config.ipt_params.idle_scale_ms = 1.0;
  config.ipt_params.idle_max_ms = 2.0;
  config.ipt_params.p_burst_stay = 0.0;
  config.ipt_params.p_idle_to_burst = 0.0;
  config.bidirectional_correlation_policy.enabled = true;
  config.bidirectional_correlation_policy.small_response_threshold_bytes = 192;
  config.bidirectional_correlation_policy.post_response_delay_jitter_ms_min = 11.0;
  config.bidirectional_correlation_policy.post_response_delay_jitter_ms_max = 11.0;
  return config;
}

struct Harness final {
  td::unique_ptr<StealthTransportDecorator> transport;
  RecordingTransport *inner{nullptr};
  MockClock *clock{nullptr};

  static Harness create() {
    Harness harness;
    auto inner = td::make_unique<RecordingTransport>();
    harness.inner = inner.get();
    auto clock = td::make_unique<MockClock>();
    harness.clock = clock.get();
    auto decorator = StealthTransportDecorator::create(std::move(inner), make_jitter_config(),
                                                       td::make_unique<MockRng>(29), std::move(clock));
    CHECK(decorator.is_ok());
    harness.transport = decorator.move_as_ok();
    return harness;
  }

  void perform_read(size_t bytes) {
    inner->next_read_message = td::BufferSlice(td::Slice(td::string(bytes, 's')));
    td::BufferSlice message;
    td::uint32 quick_ack = 0;
    auto result = transport->read_next(&message, &quick_ack);
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(bytes, result.ok());
  }

  td::uint32 perform_quick_ack(td::uint32 quick_ack_value) {
    inner->last_quick_ack = quick_ack_value;
    inner->next_read_message.clear();
    td::BufferSlice message;
    td::uint32 quick_ack = 0;
    auto result = transport->read_next(&message, &quick_ack);
    inner->last_quick_ack = 0;
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(0u, result.ok());
    ASSERT_TRUE(message.empty());
    return quick_ack;
  }

  double queue_write(TrafficHint hint, td::Slice payload = td::Slice("client-request")) {
    transport->set_traffic_hint(hint);
    td::BufferWriter writer(payload, transport->max_prepend_size(), transport->max_append_size());
    transport->write(std::move(writer), false);
    return transport->get_shaping_wakeup();
  }
};

TEST(BidirectionalResponseJitterQueueEdges, LargeInboundResponseNeutralizesQueuedInteractiveJitterBeforeFlush) {
  auto harness = Harness::create();

  harness.perform_read(96);
  auto delayed_wakeup = harness.queue_write(TrafficHint::Interactive);
  ASSERT_TRUE(delayed_wakeup > harness.clock->now());
  ASSERT_TRUE(delayed_wakeup - harness.clock->now() >= 0.011 - 1e-6);

  harness.perform_read(4096);

  auto neutralized_wakeup = harness.transport->get_shaping_wakeup();
  ASSERT_TRUE(neutralized_wakeup == 0.0 || neutralized_wakeup <= harness.clock->now());
}

TEST(BidirectionalResponseJitterQueueEdges, QuickAckDoesNotNeutralizeQueuedInteractiveJitterBeforeFlush) {
  auto harness = Harness::create();

  harness.perform_read(96);
  auto delayed_wakeup = harness.queue_write(TrafficHint::Interactive);
  ASSERT_TRUE(delayed_wakeup > harness.clock->now());
  ASSERT_TRUE(delayed_wakeup - harness.clock->now() >= 0.011 - 1e-6);

  constexpr td::uint32 kQuickAck = 0x80000031u;
  ASSERT_EQ(kQuickAck, harness.perform_quick_ack(kQuickAck));

  auto retained_wakeup = harness.transport->get_shaping_wakeup();
  ASSERT_TRUE(retained_wakeup > harness.clock->now());
  ASSERT_TRUE(retained_wakeup - harness.clock->now() >= 0.011 - 1e-6);
}

}  // namespace