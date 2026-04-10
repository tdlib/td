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
using td::mtproto::stealth::RecordSizeBin;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;

DrsPhaseModel make_exact_phase(td::int32 cap) {
  DrsPhaseModel phase;
  phase.bins = {{cap, cap, 1}};
  phase.max_repeat_run = 16;
  phase.local_jitter = 0;
  return phase;
}

StealthConfig make_greeting_edge_config(double jitter_ms) {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_exact_phase(320);
  config.drs_policy.congestion_open = make_exact_phase(320);
  config.drs_policy.steady_state = make_exact_phase(320);
  config.drs_policy.slow_start_records = 1024;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.min_payload_cap = 320;
  config.drs_policy.max_payload_cap = 320;
  config.ipt_params.burst_mu_ms = 0.0;
  config.ipt_params.burst_sigma = 0.0;
  config.ipt_params.burst_max_ms = 1.0;
  config.ipt_params.idle_alpha = 1.0;
  config.ipt_params.idle_scale_ms = 1.0;
  config.ipt_params.idle_max_ms = 2.0;
  config.ipt_params.p_burst_stay = 0.0;
  config.ipt_params.p_idle_to_burst = 0.0;

  GreetingCamouflagePolicy greeting_policy;
  greeting_policy.greeting_record_count = 2;
  greeting_policy.record_models[0] = DrsPhaseModel{{RecordSizeBin{180, 180, 1}}, 8, 0};
  greeting_policy.record_models[1] = DrsPhaseModel{{RecordSizeBin{420, 420, 1}}, 8, 0};
  config.greeting_camouflage_policy = greeting_policy;

  config.bidirectional_correlation_policy.enabled = true;
  config.bidirectional_correlation_policy.small_response_threshold_bytes = 192;
  config.bidirectional_correlation_policy.next_request_min_payload_cap = 1200;
  config.bidirectional_correlation_policy.post_response_delay_jitter_ms_min = jitter_ms;
  config.bidirectional_correlation_policy.post_response_delay_jitter_ms_max = jitter_ms;
  return config;
}

struct Harness final {
  td::unique_ptr<StealthTransportDecorator> transport;
  RecordingTransport *inner{nullptr};
  MockClock *clock{nullptr};

  static Harness create(double jitter_ms) {
    Harness harness;
    auto inner = td::make_unique<RecordingTransport>();
    harness.inner = inner.get();
    auto clock = td::make_unique<MockClock>();
    harness.clock = clock.get();
    auto decorator = StealthTransportDecorator::create(std::move(inner), make_greeting_edge_config(jitter_ms),
                                                       td::make_unique<MockRng>(41), std::move(clock));
    CHECK(decorator.is_ok());
    harness.transport = decorator.move_as_ok();
    return harness;
  }

  void perform_read(size_t bytes) {
    inner->next_read_message = td::BufferSlice(td::Slice(td::string(bytes, 'r')));
    td::BufferSlice message;
    td::uint32 quick_ack = 0;
    auto result = transport->read_next(&message, &quick_ack);
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(bytes, result.ok());
  }

  double queue_write(TrafficHint hint, td::Slice payload = td::Slice("payload")) {
    transport->set_traffic_hint(hint);
    td::BufferWriter writer(payload, transport->max_prepend_size(), transport->max_append_size());
    transport->write(std::move(writer), false);
    return transport->get_shaping_wakeup();
  }

  void flush_ready() {
    auto wakeup = transport->get_shaping_wakeup();
    if (wakeup > clock->now()) {
      clock->advance(wakeup - clock->now());
    }
    transport->pre_flush_write(clock->now());
  }
};

TEST(BidirectionalSizeCorrelationGreetingEdges, GreetingEmissionConsumesArmedFloorBeforeDrsResumes) {
  auto harness = Harness::create(0.0);

  harness.queue_write(TrafficHint::Interactive, td::Slice("first"));
  harness.flush_ready();
  ASSERT_FALSE(harness.inner->stealth_record_padding_targets.empty());
  ASSERT_EQ(180, harness.inner->stealth_record_padding_targets.back());

  harness.perform_read(88);

  harness.queue_write(TrafficHint::Interactive, td::Slice("second"));
  harness.flush_ready();
  ASSERT_EQ(420, harness.inner->stealth_record_padding_targets.back());

  harness.queue_write(TrafficHint::Interactive, td::Slice("third"));
  harness.flush_ready();
  ASSERT_EQ(320, harness.inner->stealth_record_padding_targets.back());
}

TEST(BidirectionalSizeCorrelationGreetingEdges, GreetingWriteConsumesPostResponseJitterBeforeDrsResumes) {
  auto harness = Harness::create(11.0);

  harness.queue_write(TrafficHint::Interactive, td::Slice("first"));
  harness.flush_ready();
  ASSERT_FALSE(harness.inner->stealth_record_padding_targets.empty());
  ASSERT_EQ(180, harness.inner->stealth_record_padding_targets.back());

  harness.perform_read(88);

  auto greeting_wakeup = harness.queue_write(TrafficHint::Interactive, td::Slice("second"));
  ASSERT_TRUE(greeting_wakeup > harness.clock->now());
  ASSERT_TRUE(greeting_wakeup - harness.clock->now() >= 0.011 - 1e-6);
  harness.flush_ready();
  ASSERT_EQ(420, harness.inner->stealth_record_padding_targets.back());

  auto drs_wakeup = harness.queue_write(TrafficHint::Interactive, td::Slice("third"));
  ASSERT_TRUE(drs_wakeup == 0.0 || drs_wakeup <= harness.clock->now());
}

}  // namespace