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

#include <array>
#include <cmath>

namespace {

using td::mtproto::IStreamTransport;
using td::mtproto::ProxySecret;
using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::GreetingCamouflagePolicy;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::TransportType;

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

class TargetTrackingTransport final : public IStreamTransport {
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
    written_hints.push_back(last_hint);
    written_quick_acks.push_back(quick_ack);
    written_payloads.push_back(message.as_buffer_slice().as_slice().str());
    per_write_padding_targets.push_back(current_padding_target_);
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

  void pre_flush_write(double now) final {
    last_pre_flush_now = now;
  }

  double get_shaping_wakeup() const final {
    return shaping_wakeup_result;
  }

  void set_traffic_hint(TrafficHint hint) final {
    last_hint = hint;
  }

  void set_max_tls_record_size(td::int32 size) final {
    (void)size;
  }

  void set_stealth_record_padding_target(td::int32 target_bytes) final {
    current_padding_target_ = target_bytes;
  }

  bool supports_tls_record_sizing() const final {
    return true;
  }

  double shaping_wakeup_result{0.0};
  td::ChainBufferReader *input_{nullptr};
  td::ChainBufferWriter *output_{nullptr};
  int write_calls{0};
  double last_pre_flush_now{0.0};
  TrafficHint last_hint{TrafficHint::Unknown};
  std::vector<TrafficHint> written_hints;
  std::vector<bool> written_quick_acks;
  std::vector<td::string> written_payloads;
  std::vector<td::int32> per_write_padding_targets;

 private:
  td::int32 current_padding_target_{0};
};

struct DecoratorFixture final {
  td::unique_ptr<StealthTransportDecorator> decorator;
  TargetTrackingTransport *inner{nullptr};
  MockClock *clock{nullptr};
};

struct BoundaryCase final {
  const char *name;
  std::array<size_t, 2> delayed_payloads;
  std::array<td::string, 3> expected_payloads;
  std::array<td::int32, 3> expected_targets;
  int expected_writes{0};
};

StealthConfig make_config(td::uint8 greeting_record_count) {
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
  greeting_policy.greeting_record_count = greeting_record_count;
  greeting_policy.record_models[0] = make_exact_phase(320);
  greeting_policy.record_models[1] = make_exact_phase(640);
  greeting_policy.record_models[2] = make_exact_phase(960);
  config.greeting_camouflage_policy = greeting_policy;
  return config;
}

DecoratorFixture make_fixture(td::uint8 greeting_record_count) {
  auto inner = td::make_unique<TargetTrackingTransport>();
  auto *inner_ptr = inner.get();
  auto clock = td::make_unique<MockClock>();
  auto *clock_ptr = clock.get();
  auto decorator = StealthTransportDecorator::create(std::move(inner), make_config(greeting_record_count),
                                                     td::make_unique<MockRng>(7), std::move(clock));
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr, clock_ptr};
}

void enqueue_packet(DecoratorFixture &fixture, size_t payload_size) {
  fixture.decorator->set_traffic_hint(TrafficHint::Interactive);
  fixture.decorator->write(make_test_buffer(payload_size), false);
}

double leave_delayed_interactive_queued(DecoratorFixture &fixture, size_t immediate_payload_size,
                                        const std::array<size_t, 2> &delayed_payloads) {
  enqueue_packet(fixture, immediate_payload_size);
  enqueue_packet(fixture, delayed_payloads[0]);
  enqueue_packet(fixture, delayed_payloads[1]);
  fixture.decorator->pre_flush_write(fixture.clock->now());
  auto wakeup = fixture.decorator->get_shaping_wakeup();
  ASSERT_TRUE(wakeup > fixture.clock->now());
  return wakeup;
}

void assert_sequence(const DecoratorFixture &fixture, const BoundaryCase &test_case) {
  ASSERT_EQ(test_case.expected_writes, fixture.inner->write_calls);
  ASSERT_EQ(static_cast<size_t>(test_case.expected_writes), fixture.inner->written_payloads.size());
  ASSERT_EQ(static_cast<size_t>(test_case.expected_writes), fixture.inner->per_write_padding_targets.size());
  for (int i = 0; i < test_case.expected_writes; i++) {
    ASSERT_EQ(test_case.expected_payloads[static_cast<size_t>(i)],
              fixture.inner->written_payloads[static_cast<size_t>(i)]);
    ASSERT_EQ(test_case.expected_targets[static_cast<size_t>(i)],
              fixture.inner->per_write_padding_targets[static_cast<size_t>(i)]);
  }
}

TEST(PostHandshakeGreetingCapBoundaryMatrixAdversarial,
     GreetingCapBoundaryMatrixCoalescesBelowThresholdAndSplitsAboveThreshold) {
  const std::array<BoundaryCase, 2> kCases = {{
      {"below_threshold", {639, 1}, {td::string(300, 'x'), td::string(640, 'x'), td::string()}, {320, 640, 0}, 2},
      {"above_threshold",
       {639, 2},
       {td::string(300, 'x'), td::string(639, 'x'), td::string(2, 'x')},
       {320, 640, 1400},
       3},
  }};

  for (const auto &test_case : kCases) {
    auto fixture = make_fixture(2);
    auto wakeup = leave_delayed_interactive_queued(fixture, 300, test_case.delayed_payloads);

    fixture.clock->advance(wakeup - fixture.clock->now());
    fixture.decorator->pre_flush_write(fixture.clock->now());

    assert_sequence(fixture, test_case);
  }
}

TEST(PostHandshakeGreetingCapBoundaryMatrixAdversarial,
     PostGreetingCapBoundaryMatrixCoalescesBelowThresholdAndSplitsAboveThreshold) {
  const std::array<BoundaryCase, 2> kCases = {{
      {"below_threshold", {1399, 1}, {td::string(300, 'x'), td::string(1400, 'x'), td::string()}, {320, 1400, 0}, 2},
      {"above_threshold",
       {1399, 2},
       {td::string(300, 'x'), td::string(1399, 'x'), td::string(2, 'x')},
       {320, 1400, 1400},
       3},
  }};

  for (const auto &test_case : kCases) {
    auto fixture = make_fixture(1);
    auto wakeup = leave_delayed_interactive_queued(fixture, 300, test_case.delayed_payloads);

    fixture.clock->advance(wakeup - fixture.clock->now());
    fixture.decorator->pre_flush_write(fixture.clock->now());

    assert_sequence(fixture, test_case);
  }
}

}  // namespace