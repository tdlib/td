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

using td::mtproto::stealth::ChaffPolicy;
using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::RecordSizeBin;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;

DrsPhaseModel make_exact_phase(td::int32 cap) {
  DrsPhaseModel phase;
  phase.bins = {RecordSizeBin{cap, cap, 1}};
  phase.max_repeat_run = 64;
  phase.local_jitter = 0;
  return phase;
}

StealthConfig make_config() {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.greeting_camouflage_policy.greeting_record_count = 0;
  config.drs_policy.slow_start = make_exact_phase(320);
  config.drs_policy.congestion_open = make_exact_phase(320);
  config.drs_policy.steady_state = make_exact_phase(320);
  config.drs_policy.slow_start_records = 1024;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.idle_reset_ms_min = 1000;
  config.drs_policy.idle_reset_ms_max = 1000;
  config.drs_policy.min_payload_cap = 320;
  config.drs_policy.max_payload_cap = 320;

  config.ipt_params.burst_mu_ms = 0.0;
  config.ipt_params.burst_sigma = 0.0;
  config.ipt_params.burst_max_ms = 1.0;
  config.ipt_params.idle_alpha = 1.0;
  config.ipt_params.idle_scale_ms = 25.0;
  config.ipt_params.idle_max_ms = 50.0;
  config.ipt_params.p_burst_stay = 0.0;
  config.ipt_params.p_idle_to_burst = 0.0;

  config.bidirectional_correlation_policy.enabled = true;
  config.bidirectional_correlation_policy.small_response_threshold_bytes = 192;
  config.bidirectional_correlation_policy.next_request_min_payload_cap = 1200;
  config.bidirectional_correlation_policy.post_response_delay_jitter_ms_min = 0.0;
  config.bidirectional_correlation_policy.post_response_delay_jitter_ms_max = 0.0;

  config.chaff_policy = ChaffPolicy{};
  config.chaff_policy.enabled = true;
  config.chaff_policy.idle_threshold_ms = 5000;
  config.chaff_policy.min_interval_ms = 10.0;
  config.chaff_policy.max_bytes_per_minute = 8192;
  config.chaff_policy.record_model = make_exact_phase(384);
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
    auto decorator = StealthTransportDecorator::create(std::move(inner), make_config(), td::make_unique<MockRng>(53),
                                                       std::move(clock));
    CHECK(decorator.is_ok());
    harness.transport = decorator.move_as_ok();
    return harness;
  }

  void perform_inbound_read(size_t bytes) {
    inner->next_read_message = td::BufferSlice(td::Slice(td::string(bytes, 'r')));
    td::BufferSlice message;
    td::uint32 quick_ack = 0;
    auto result = transport->read_next(&message, &quick_ack);
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(bytes, result.ok());
  }

  void perform_zero_length_inbound_frame() {
    inner->last_quick_ack = 0;
    inner->next_read_message.clear();
    td::BufferSlice message;
    td::uint32 quick_ack = 0;
    auto result = transport->read_next(&message, &quick_ack);
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(0u, result.ok());
    ASSERT_EQ(0u, quick_ack);
    ASSERT_TRUE(message.empty());
  }

  void queue_interactive_write(td::Slice payload = td::Slice("client")) {
    transport->set_traffic_hint(TrafficHint::Interactive);
    td::BufferWriter writer(payload, transport->max_prepend_size(), transport->max_append_size());
    transport->write(std::move(writer), false);
  }

  void flush_until_write() {
    auto writes_before = inner->write_calls;
    while (inner->write_calls == writes_before) {
      transport->pre_flush_write(clock->now());
      if (inner->write_calls != writes_before) {
        break;
      }
      auto wakeup = transport->get_shaping_wakeup();
      ASSERT_TRUE(wakeup > clock->now());
      clock->advance(wakeup - clock->now());
    }
  }
};

TEST(BidirectionalZeroLengthChaffIntegration,
     ZeroLengthInboundFrameRearmsIdleThresholdWithoutClearingInteractiveFloor) {
  auto harness = Harness::create();

  auto initial_wakeup = harness.transport->get_shaping_wakeup();
  ASSERT_TRUE(initial_wakeup >= harness.clock->now() + 5.0 - 1e-6);

  harness.perform_inbound_read(128);
  harness.clock->advance(4.0);
  harness.perform_zero_length_inbound_frame();

  auto rearmed_wakeup = harness.transport->get_shaping_wakeup();
  ASSERT_TRUE(rearmed_wakeup >= harness.clock->now() + 5.0 - 1e-6);

  harness.clock->advance(4.0);
  harness.transport->pre_flush_write(harness.clock->now());
  ASSERT_EQ(0, harness.inner->write_calls);

  harness.queue_interactive_write();
  harness.flush_until_write();

  ASSERT_EQ(1, harness.inner->write_calls);
  ASSERT_FALSE(harness.inner->stealth_record_padding_targets.empty());
  ASSERT_EQ(1200, harness.inner->stealth_record_padding_targets.back());
  ASSERT_FALSE(harness.inner->queued_hints.empty());
  ASSERT_EQ(TrafficHint::Interactive, harness.inner->queued_hints.back());
}

TEST(BidirectionalZeroLengthChaffIntegration,
     ZeroLengthInboundFrameAllowsDelayedChaffWithoutConsumingInteractiveFloor) {
  auto harness = Harness::create();

  harness.perform_inbound_read(128);
  harness.clock->advance(4.0);
  harness.perform_zero_length_inbound_frame();

  harness.flush_until_write();

  ASSERT_EQ(1, harness.inner->write_calls);
  ASSERT_FALSE(harness.inner->stealth_record_padding_targets.empty());
  ASSERT_EQ(384, harness.inner->stealth_record_padding_targets.back());
  ASSERT_FALSE(harness.inner->queued_hints.empty());
  ASSERT_EQ(TrafficHint::Keepalive, harness.inner->queued_hints.back());
  ASSERT_FALSE(harness.inner->written_payloads.empty());
  ASSERT_TRUE(harness.inner->written_payloads.back().empty());

  harness.queue_interactive_write();
  harness.flush_until_write();

  ASSERT_EQ(2, harness.inner->write_calls);
  ASSERT_EQ(1200, harness.inner->stealth_record_padding_targets.back());
  ASSERT_EQ(TrafficHint::Interactive, harness.inner->queued_hints.back());
}

}  // namespace