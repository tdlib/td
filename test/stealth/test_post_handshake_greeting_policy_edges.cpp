// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockClock.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/RecordingTransport.h"

#include "td/mtproto/IStreamTransport.h"
#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"

#include "td/utils/buffer.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::IStreamTransport;
using td::mtproto::ProxySecret;
using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::GreetingCamouflagePolicy;
using td::mtproto::stealth::RecordSizeBin;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;
using td::mtproto::TransportType;

constexpr td::int32 kMinBrowserGreetingRecordSize = 80;
constexpr td::int32 kMaxBrowserGreetingRecordSize = 1500;

DrsPhaseModel make_exact_phase(td::int32 record_size) {
  DrsPhaseModel phase;
  phase.bins = {{record_size, record_size, 1}};
  phase.max_repeat_run = 1;
  phase.local_jitter = 0;
  return phase;
}

DrsPhaseModel make_range_phase(std::initializer_list<RecordSizeBin> bins) {
  DrsPhaseModel phase;
  phase.bins.assign(bins.begin(), bins.end());
  phase.max_repeat_run = 4;
  phase.local_jitter = 0;
  return phase;
}

td::BufferWriter make_test_buffer(td::Slice payload) {
  return td::BufferWriter(payload, 32, 0);
}

StealthConfig make_budget_stress_config() {
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
  config.drs_policy.max_payload_cap = 4096;

  config.record_padding_policy.small_record_threshold = 2048;
  config.record_padding_policy.small_record_max_fraction = 0.0;
  config.record_padding_policy.small_record_window_size = 8;

  GreetingCamouflagePolicy greeting_policy;
  greeting_policy.greeting_record_count = 2;
  greeting_policy.record_models[0] = make_range_phase({{120, 140, 1}});
  greeting_policy.record_models[1] = make_range_phase({{160, 180, 1}});
  config.greeting_camouflage_policy = greeting_policy;
  return config;
}

struct DecoratorFixture final {
  td::unique_ptr<StealthTransportDecorator> decorator;
  RecordingTransport *inner{nullptr};
  MockClock *clock{nullptr};
};

DecoratorFixture make_fixture(StealthConfig config, td::uint64 seed = 7) {
  auto inner = td::make_unique<RecordingTransport>();
  inner->supports_tls_record_sizing_result = true;
  auto *inner_ptr = inner.get();

  auto clock = td::make_unique<MockClock>();
  auto *clock_ptr = clock.get();

  auto decorator = StealthTransportDecorator::create(std::move(inner), std::move(config),
                                                     td::make_unique<MockRng>(seed), std::move(clock));
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr, clock_ptr};
}

void flush_once(DecoratorFixture &fixture, td::Slice payload = "x", TrafficHint hint = TrafficHint::Interactive) {
  fixture.decorator->set_traffic_hint(hint);
  fixture.decorator->write(make_test_buffer(payload), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());
  auto wakeup = fixture.decorator->get_shaping_wakeup();
  if (wakeup > fixture.clock->now()) {
    fixture.clock->advance(wakeup - fixture.clock->now());
    fixture.decorator->pre_flush_write(fixture.clock->now());
  }
}

class GreetingOverheadTransport final : public IStreamTransport {
 public:
  td::Result<size_t> read_next(td::BufferSlice *message, td::uint32 *quick_ack) final {
    (void)message;
    (void)quick_ack;
    return 0;
  }

  bool support_quick_ack() const final {
    return true;
  }

  void write(td::BufferWriter &&message, bool quick_ack) final {
    write_calls++;
    written_quick_acks.push_back(quick_ack);
    written_payloads.push_back(message.as_buffer_slice().as_slice().str());
  }

  bool can_read() const final {
    return false;
  }

  bool can_write() const final {
    return true;
  }

  void init(td::ChainBufferReader *input, td::ChainBufferWriter *output) final {
    input_ = input;
    output_ = output;
  }

  size_t max_prepend_size() const final {
    return 17;
  }

  size_t max_append_size() const final {
    return 9;
  }

  TransportType get_type() const final {
    return TransportType{TransportType::ObfuscatedTcp, 0, ProxySecret()};
  }

  bool use_random_padding() const final {
    return false;
  }

  void set_traffic_hint(TrafficHint hint) final {
    last_hint = hint;
  }

  void set_max_tls_record_size(td::int32 size) final {
    max_tls_record_sizes.push_back(size);
  }

  void set_stealth_record_padding_target(td::int32 target_bytes) final {
    stealth_record_padding_targets.push_back(target_bytes);
  }

  bool supports_tls_record_sizing() const final {
    return true;
  }

  td::int32 tls_record_sizing_payload_overhead() const final {
    return payload_overhead_result;
  }

  td::ChainBufferReader *input_{nullptr};
  td::ChainBufferWriter *output_{nullptr};
  int write_calls{0};
  TrafficHint last_hint{TrafficHint::Unknown};
  td::int32 payload_overhead_result{0};
  std::vector<bool> written_quick_acks;
  std::vector<td::string> written_payloads;
  std::vector<td::int32> max_tls_record_sizes;
  std::vector<td::int32> stealth_record_padding_targets;
};

struct OverheadFixture final {
  td::unique_ptr<StealthTransportDecorator> decorator;
  GreetingOverheadTransport *inner{nullptr};
  MockClock *clock{nullptr};
};

OverheadFixture make_overhead_fixture(td::int32 payload_overhead) {
  auto config = make_budget_stress_config();
  config.record_padding_policy.small_record_threshold = 200;
  config.record_padding_policy.small_record_max_fraction = 1.0;
  config.greeting_camouflage_policy.record_models[0] = make_range_phase({{1400, 1400, 1}});

  auto inner = td::make_unique<GreetingOverheadTransport>();
  inner->payload_overhead_result = payload_overhead;
  auto *inner_ptr = inner.get();
  auto clock = td::make_unique<MockClock>();
  auto *clock_ptr = clock.get();
  auto decorator = StealthTransportDecorator::create(std::move(inner), std::move(config), td::make_unique<MockRng>(9),
                                                     std::move(clock));
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr, clock_ptr};
}

void flush_once(OverheadFixture &fixture, td::Slice payload = "x", TrafficHint hint = TrafficHint::Interactive) {
  fixture.decorator->set_traffic_hint(hint);
  fixture.decorator->write(make_test_buffer(payload), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());
  auto wakeup = fixture.decorator->get_shaping_wakeup();
  if (wakeup > fixture.clock->now()) {
    fixture.clock->advance(wakeup - fixture.clock->now());
    fixture.decorator->pre_flush_write(fixture.clock->now());
  }
}

TEST(PostHandshakeGreetingPolicyEdges, SmallRecordBudgetCannotPushGreetingPastBrowserFirstFlightCeiling) {
  auto fixture = make_fixture(make_budget_stress_config());

  flush_once(fixture, "first");

  ASSERT_FALSE(fixture.inner->stealth_record_padding_targets.empty());
  auto target = fixture.inner->stealth_record_padding_targets.back();
  ASSERT_TRUE(target >= kMinBrowserGreetingRecordSize);
  ASSERT_TRUE(target <= kMaxBrowserGreetingRecordSize);
  ASSERT_FALSE(fixture.inner->max_tls_record_sizes.empty());
  ASSERT_TRUE(fixture.inner->max_tls_record_sizes.back() <= kMaxBrowserGreetingRecordSize);
}

TEST(PostHandshakeGreetingPolicyEdges, SmallRecordBudgetCannotPushSecondGreetingPastBrowserFirstFlightCeiling) {
  auto fixture = make_fixture(make_budget_stress_config());

  flush_once(fixture, "first");
  flush_once(fixture, "second");

  ASSERT_TRUE(fixture.inner->stealth_record_padding_targets.size() >= 2u);
  auto target = fixture.inner->stealth_record_padding_targets.back();
  ASSERT_TRUE(target >= kMinBrowserGreetingRecordSize);
  ASSERT_TRUE(target <= kMaxBrowserGreetingRecordSize);
  ASSERT_TRUE(fixture.inner->max_tls_record_sizes.size() >= 2u);
  ASSERT_TRUE(fixture.inner->max_tls_record_sizes.back() <= kMaxBrowserGreetingRecordSize);
}

TEST(PostHandshakeGreetingPolicyEdges, GreetingAppliesTransportPayloadOverheadToWireRecordSize) {
  auto fixture = make_overhead_fixture(200);

  flush_once(fixture, "first");

  ASSERT_FALSE(fixture.inner->stealth_record_padding_targets.empty());
  ASSERT_EQ(1400, fixture.inner->stealth_record_padding_targets.back());
  ASSERT_FALSE(fixture.inner->max_tls_record_sizes.empty());
  ASSERT_EQ(1600, fixture.inner->max_tls_record_sizes.back());
}

}  // namespace