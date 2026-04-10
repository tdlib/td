// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockClock.h"
#include "test/stealth/MockRng.h"

#include "td/mtproto/IStreamTransport.h"
#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"

#include "td/utils/buffer.h"
#include "td/utils/tests.h"

#include <vector>

namespace {

using td::mtproto::IStreamTransport;
using td::mtproto::ProxySecret;
using td::mtproto::stealth::ChaffPolicy;
using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::RecordSizeBin;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::TransportType;

class ChaffAwareTransport final : public IStreamTransport {
 public:
  td::Result<size_t> read_next(td::BufferSlice *message, td::uint32 *quick_ack) final {
    *quick_ack = 0;
    if (next_read_message.empty()) {
      message->clear();
      return 0;
    }
    *message = next_read_message.clone();
    return next_read_message.size();
  }

  bool support_quick_ack() const final {
    return true;
  }

  void write(td::BufferWriter &&message, bool quick_ack) final {
    write_calls++;
    emitted_targets.push_back(current_target_);
    emitted_hints.push_back(last_hint_);
    emitted_payload_sizes.push_back(message.size());
    emitted_quick_acks.push_back(quick_ack);
  }

  bool can_read() const final {
    return true;
  }

  bool can_write() const final {
    return true;
  }

  void init(td::ChainBufferReader *input, td::ChainBufferWriter *output) final {
    input_ = input;
    output_ = output;
  }

  size_t max_prepend_size() const final {
    return 32;
  }

  size_t max_append_size() const final {
    return 4096;
  }

  TransportType get_type() const final {
    return TransportType{TransportType::ObfuscatedTcp, 0, ProxySecret()};
  }

  bool use_random_padding() const final {
    return false;
  }

  void set_traffic_hint(TrafficHint hint) final {
    last_hint_ = hint;
  }

  void set_max_tls_record_size(td::int32 size) final {
    max_record_sizes.push_back(size);
  }

  void set_stealth_record_padding_target(td::int32 target_bytes) final {
    current_target_ = target_bytes;
  }

  bool supports_tls_record_sizing() const final {
    return true;
  }

  td::int32 tls_record_sizing_payload_overhead() const final {
    return 0;
  }

  td::ChainBufferReader *input_{nullptr};
  td::ChainBufferWriter *output_{nullptr};
  td::BufferSlice next_read_message;
  td::int32 current_target_{-1};
  int write_calls{0};
  TrafficHint last_hint_{TrafficHint::Unknown};
  std::vector<td::int32> max_record_sizes;
  std::vector<td::int32> emitted_targets;
  std::vector<TrafficHint> emitted_hints;
  std::vector<size_t> emitted_payload_sizes;
  std::vector<bool> emitted_quick_acks;
};

DrsPhaseModel make_exact_phase(td::int32 cap) {
  DrsPhaseModel phase;
  phase.bins = {RecordSizeBin{cap, cap, 1}};
  phase.max_repeat_run = 64;
  phase.local_jitter = 0;
  return phase;
}

StealthConfig make_config(double jitter_ms) {
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
  config.bidirectional_correlation_policy.post_response_delay_jitter_ms_min = jitter_ms;
  config.bidirectional_correlation_policy.post_response_delay_jitter_ms_max = jitter_ms;

  config.chaff_policy = ChaffPolicy{};
  config.chaff_policy.enabled = true;
  config.chaff_policy.idle_threshold_ms = 5000;
  config.chaff_policy.min_interval_ms = 10.0;
  config.chaff_policy.max_bytes_per_minute = 8192;
  config.chaff_policy.record_model = make_exact_phase(384);
  return config;
}

td::BufferWriter make_test_buffer(td::Slice payload, size_t prepend, size_t append) {
  return td::BufferWriter(payload, prepend, append);
}

struct Fixture final {
  td::unique_ptr<StealthTransportDecorator> transport;
  ChaffAwareTransport *inner{nullptr};
  MockClock *clock{nullptr};

  static Fixture create(double jitter_ms, td::uint64 seed = 17) {
    Fixture fixture;
    auto inner = td::make_unique<ChaffAwareTransport>();
    fixture.inner = inner.get();
    auto clock = td::make_unique<MockClock>();
    fixture.clock = clock.get();
    auto decorator = StealthTransportDecorator::create(std::move(inner), make_config(jitter_ms),
                                                       td::make_unique<MockRng>(seed), std::move(clock));
    CHECK(decorator.is_ok());
    fixture.transport = decorator.move_as_ok();
    return fixture;
  }
};

void flush_until_next_write(Fixture &fixture) {
  auto writes_before = fixture.inner->write_calls;
  while (fixture.inner->write_calls == writes_before) {
    fixture.transport->pre_flush_write(fixture.clock->now());
    if (fixture.inner->write_calls != writes_before) {
      break;
    }
    auto wakeup = fixture.transport->get_shaping_wakeup();
    ASSERT_TRUE(wakeup > fixture.clock->now());
    fixture.clock->advance(wakeup - fixture.clock->now());
  }
}

void simulate_small_inbound_response(Fixture &fixture, size_t bytes) {
  fixture.inner->next_read_message = td::BufferSlice(td::Slice(td::string(bytes, 'r')));
  td::BufferSlice message;
  td::uint32 quick_ack = 0;
  auto result = fixture.transport->read_next(&message, &quick_ack);
  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(bytes, result.ok());
  fixture.inner->next_read_message.clear();
}

void queue_interactive_write(Fixture &fixture, td::Slice payload = "client") {
  fixture.transport->set_traffic_hint(TrafficHint::Interactive);
  fixture.transport->write(
      make_test_buffer(payload, fixture.transport->max_prepend_size(), fixture.transport->max_append_size()), false);
}

TEST(BidirectionalSizeCorrelationChaffEdges, IdleChaffDoesNotConsumeArmedInteractiveFloor) {
  auto fixture = Fixture::create(0.0);

  simulate_small_inbound_response(fixture, 128);
  flush_until_next_write(fixture);

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(std::vector<td::int32>({384}), fixture.inner->emitted_targets);
  ASSERT_EQ(std::vector<TrafficHint>({TrafficHint::Keepalive}), fixture.inner->emitted_hints);
  ASSERT_EQ(std::vector<size_t>({0u}), fixture.inner->emitted_payload_sizes);

  queue_interactive_write(fixture);
  flush_until_next_write(fixture);

  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(1200, fixture.inner->emitted_targets.back());
  ASSERT_EQ(TrafficHint::Interactive, fixture.inner->emitted_hints.back());
}

TEST(BidirectionalSizeCorrelationChaffEdges, IdleChaffDoesNotClearPostResponseJitter) {
  auto fixture = Fixture::create(11.0);

  simulate_small_inbound_response(fixture, 128);
  flush_until_next_write(fixture);

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(384, fixture.inner->emitted_targets.back());

  queue_interactive_write(fixture);
  auto wakeup = fixture.transport->get_shaping_wakeup();
  ASSERT_TRUE(wakeup > fixture.clock->now());
  ASSERT_TRUE(wakeup - fixture.clock->now() >= 0.011 - 1e-6);

  flush_until_next_write(fixture);

  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(1200, fixture.inner->emitted_targets.back());
  ASSERT_EQ(TrafficHint::Interactive, fixture.inner->emitted_hints.back());
}

}  // namespace