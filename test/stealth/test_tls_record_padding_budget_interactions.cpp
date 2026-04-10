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
using td::mtproto::PacketInfo;
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

class BudgetProbeTransport final : public IStreamTransport {
 public:
  td::Result<size_t> read_next(td::BufferSlice *message, td::uint32 *quick_ack) final {
    *quick_ack = 0;
    message->clear();
    return 0;
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

  void configure_packet_info(PacketInfo *packet_info) const final {
    CHECK(packet_info != nullptr);
    packet_info->use_random_padding = false;
  }

  void set_traffic_hint(TrafficHint hint) final {
    last_hint_ = hint;
  }

  void set_max_tls_record_size(td::int32 size) final {
    max_record_sizes.push_back(size);
  }

  void set_stealth_record_padding_target(td::int32 target_bytes) final {
    current_target_ = target_bytes;
    target_updates.push_back(target_bytes);
  }

  bool supports_tls_record_sizing() const final {
    return true;
  }

  td::int32 tls_record_sizing_payload_overhead() const final {
    return 0;
  }

  td::ChainBufferReader *input_{nullptr};
  td::ChainBufferWriter *output_{nullptr};
  td::int32 current_target_{-1};
  int write_calls{0};
  TrafficHint last_hint_{TrafficHint::Unknown};
  std::vector<td::int32> target_updates;
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

StealthConfig make_budget_interaction_config(bool enable_chaff) {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_exact_phase(256);
  config.drs_policy.congestion_open = make_exact_phase(256);
  config.drs_policy.steady_state = make_exact_phase(256);
  config.drs_policy.slow_start_records = 1024;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.idle_reset_ms_min = 1000;
  config.drs_policy.idle_reset_ms_max = 1000;
  config.drs_policy.min_payload_cap = 256;
  config.drs_policy.max_payload_cap = 256;

  config.record_padding_policy.small_record_threshold = 400;
  config.record_padding_policy.small_record_max_fraction = 0.2;
  config.record_padding_policy.small_record_window_size = 5;

  config.ipt_params.burst_mu_ms = 0.0;
  config.ipt_params.burst_sigma = 0.0;
  config.ipt_params.burst_max_ms = 1.0;
  config.ipt_params.idle_alpha = 1.0;
  config.ipt_params.idle_scale_ms = 25.0;
  config.ipt_params.idle_max_ms = 50.0;
  config.ipt_params.p_burst_stay = 0.0;
  config.ipt_params.p_idle_to_burst = 0.0;

  config.chaff_policy = ChaffPolicy{};
  config.chaff_policy.enabled = enable_chaff;
  config.chaff_policy.idle_threshold_ms = 5000;
  config.chaff_policy.min_interval_ms = 10.0;
  config.chaff_policy.max_bytes_per_minute = 16384;
  config.chaff_policy.record_model = make_exact_phase(256);
  return config;
}

td::BufferWriter make_test_buffer(size_t size, size_t prepend, size_t append) {
  return td::BufferWriter(td::Slice(td::string(size, 'x')), prepend, append);
}

struct Fixture final {
  td::unique_ptr<StealthTransportDecorator> decorator;
  BudgetProbeTransport *inner{nullptr};
  MockClock *clock{nullptr};

  static Fixture create(StealthConfig config, td::uint64 seed = 17) {
    Fixture fixture;
    auto inner = td::make_unique<BudgetProbeTransport>();
    fixture.inner = inner.get();
    auto clock = td::make_unique<MockClock>();
    fixture.clock = clock.get();
    auto decorator = StealthTransportDecorator::create(std::move(inner), std::move(config),
                                                       td::make_unique<MockRng>(seed), std::move(clock));
    CHECK(decorator.is_ok());
    fixture.decorator = decorator.move_as_ok();
    return fixture;
  }
};

void queue_write(Fixture &fixture, size_t payload_size, TrafficHint hint) {
  fixture.decorator->set_traffic_hint(hint);
  fixture.decorator->write(
      make_test_buffer(payload_size, fixture.decorator->max_prepend_size(), fixture.decorator->max_append_size()),
      false);
}

void flush_ready(Fixture &fixture) {
  fixture.decorator->pre_flush_write(fixture.clock->now());
  auto wakeup = fixture.decorator->get_shaping_wakeup();
  if (wakeup > fixture.clock->now()) {
    fixture.clock->advance(wakeup - fixture.clock->now());
    fixture.decorator->pre_flush_write(fixture.clock->now());
  }
}

void flush_current_deadline_only(Fixture &fixture) {
  fixture.decorator->pre_flush_write(fixture.clock->now());
}

void advance_to_next_wakeup_and_flush(Fixture &fixture) {
  auto wakeup = fixture.decorator->get_shaping_wakeup();
  ASSERT_TRUE(wakeup > fixture.clock->now());
  fixture.clock->advance(wakeup - fixture.clock->now());
  fixture.decorator->pre_flush_write(fixture.clock->now());
}

TEST(TlsRecordPaddingBudgetInteractions, CoalescedSmallWritesConsumeSingleBudgetSample) {
  auto fixture = Fixture::create(make_budget_interaction_config(false));

  queue_write(fixture, 8, TrafficHint::BulkData);
  queue_write(fixture, 12, TrafficHint::BulkData);
  queue_write(fixture, 16, TrafficHint::BulkData);
  ASSERT_EQ(0, fixture.inner->write_calls);

  flush_ready(fixture);

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(1u, fixture.inner->emitted_targets.size());
  ASSERT_EQ(256, fixture.inner->emitted_targets[0]);
  ASSERT_EQ(TrafficHint::BulkData, fixture.inner->emitted_hints[0]);

  for (int i = 0; i < 6; i++) {
    queue_write(fixture, static_cast<size_t>(24 + i), TrafficHint::BulkData);
    flush_ready(fixture);
  }

  std::vector<td::int32> expected_targets = {256, 400, 400, 400, 400, 400, 256};
  ASSERT_EQ(expected_targets, fixture.inner->emitted_targets);
}

TEST(TlsRecordPaddingBudgetInteractions, IdleChaffHonorsRollingSmallRecordBudgetUntilRecovery) {
  auto fixture = Fixture::create(make_budget_interaction_config(true));

  queue_write(fixture, 8, TrafficHint::BulkData);
  flush_current_deadline_only(fixture);

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(1u, fixture.inner->emitted_targets.size());
  ASSERT_EQ(256, fixture.inner->emitted_targets[0]);

  for (int i = 0; i < 5; i++) {
    advance_to_next_wakeup_and_flush(fixture);
    ASSERT_EQ(static_cast<size_t>(i + 2), fixture.inner->emitted_targets.size());
    ASSERT_EQ(TrafficHint::Keepalive, fixture.inner->emitted_hints.back());
    ASSERT_EQ(0u, fixture.inner->emitted_payload_sizes.back());
    ASSERT_EQ(400, fixture.inner->emitted_targets.back());
  }

  advance_to_next_wakeup_and_flush(fixture);
  ASSERT_EQ(7u, fixture.inner->emitted_targets.size());
  ASSERT_EQ(TrafficHint::Keepalive, fixture.inner->emitted_hints.back());
  ASSERT_EQ(0u, fixture.inner->emitted_payload_sizes.back());
  ASSERT_EQ(256, fixture.inner->emitted_targets.back());
}

}  // namespace