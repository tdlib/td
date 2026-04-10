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

constexpr std::array<size_t, 3> kDelayedInteractiveSizes = {480, 520, 540};
constexpr td::int32 kOverrideTarget = 1500;

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
    if (writes_per_flush_budget_result >= 0 && remaining_writes_in_cycle_ > 0) {
      remaining_writes_in_cycle_--;
    }
  }

  bool can_read() const final {
    return false;
  }

  bool can_write() const final {
    if (!can_write_result) {
      return false;
    }
    if (writes_per_flush_budget_result < 0) {
      active_writes_per_flush_budget_ = -1;
      return true;
    }
    if (remaining_writes_in_cycle_ < 0 || active_writes_per_flush_budget_ != writes_per_flush_budget_result) {
      active_writes_per_flush_budget_ = writes_per_flush_budget_result;
      remaining_writes_in_cycle_ = writes_per_flush_budget_result;
    }
    return remaining_writes_in_cycle_ > 0;
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
    if (writes_per_flush_budget_result >= 0) {
      active_writes_per_flush_budget_ = writes_per_flush_budget_result;
      remaining_writes_in_cycle_ = writes_per_flush_budget_result;
    } else {
      active_writes_per_flush_budget_ = -1;
      remaining_writes_in_cycle_ = -1;
    }
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

  bool can_write_result{true};
  int writes_per_flush_budget_result{-1};
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
  mutable int remaining_writes_in_cycle_{-1};
  mutable int active_writes_per_flush_budget_{-1};
};

struct DecoratorFixture final {
  td::unique_ptr<StealthTransportDecorator> decorator;
  TargetTrackingTransport *inner{nullptr};
  MockClock *clock{nullptr};
};

struct MatrixCase final {
  const char *name;
  std::array<int, 3> arrival_cycles;
  std::array<TrafficHint, 8> storm_hints;
  std::array<int, 8> budgets;
  int override_cycle;
};

StealthConfig make_config() {
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

DecoratorFixture make_fixture() {
  auto inner = td::make_unique<TargetTrackingTransport>();
  auto *inner_ptr = inner.get();
  auto clock = td::make_unique<MockClock>();
  auto *clock_ptr = clock.get();
  auto decorator =
      StealthTransportDecorator::create(std::move(inner), make_config(), td::make_unique<MockRng>(7), std::move(clock));
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr, clock_ptr};
}

void enqueue_packet(DecoratorFixture &fixture, size_t payload_size, TrafficHint hint, bool quick_ack) {
  fixture.decorator->set_traffic_hint(hint);
  fixture.decorator->write(make_test_buffer(payload_size), quick_ack);
}

void assert_payload_order(const std::vector<td::string> &payloads, const std::array<size_t, 3> &payload_sizes) {
  size_t expected_index = 0;
  for (const auto &payload : payloads) {
    if (expected_index == payload_sizes.size()) {
      break;
    }

    size_t matched_length = 0;
    size_t lookahead_index = expected_index;
    while (lookahead_index < payload_sizes.size() && matched_length < payload.size()) {
      matched_length += payload_sizes[lookahead_index];
      lookahead_index++;
    }

    if (matched_length == payload.size()) {
      expected_index = lookahead_index;
    }
  }

  ASSERT_EQ(payload_sizes.size(), expected_index);
}

bool all_targets_from_index_are(const std::vector<td::int32> &targets, size_t start_index, td::int32 expected) {
  if (start_index > targets.size()) {
    return false;
  }
  for (size_t i = start_index; i < targets.size(); i++) {
    if (targets[i] != expected) {
      return false;
    }
  }
  return true;
}

TEST(PostHandshakeGreetingOverrideCrossMatrixAdversarial,
     DelayedArrivalStormOverrideMatricesPreserveBacklogOrderAndForcePostOverrideTarget) {
  const std::array<MatrixCase, 2> kCases = {{
      {"override_after_first_arrival",
       {0, 2, 4},
       {TrafficHint::AuthHandshake, TrafficHint::Keepalive, TrafficHint::AuthHandshake, TrafficHint::Keepalive,
        TrafficHint::AuthHandshake, TrafficHint::Keepalive, TrafficHint::AuthHandshake, TrafficHint::Keepalive},
       {1, 0, 1, 1, 1, 0, 1, 1},
       2},
      {"override_before_second_arrival",
       {1, 1, 3},
       {TrafficHint::Keepalive, TrafficHint::AuthHandshake, TrafficHint::Keepalive, TrafficHint::AuthHandshake,
        TrafficHint::Keepalive, TrafficHint::AuthHandshake, TrafficHint::Keepalive, TrafficHint::AuthHandshake},
       {0, 1, 0, 1, 1, 1, 0, 1},
       1},
  }};

  for (const auto &test_case : kCases) {
    auto fixture = make_fixture();

    enqueue_packet(fixture, 300, TrafficHint::Interactive, false);
    fixture.decorator->pre_flush_write(fixture.clock->now());
    ASSERT_EQ(1, fixture.inner->write_calls);
    ASSERT_EQ(320, fixture.inner->per_write_padding_targets[0]);

    size_t writes_before_override = fixture.inner->write_calls;
    for (size_t cycle = 0; cycle < test_case.budgets.size(); cycle++) {
      for (size_t i = 0; i < kDelayedInteractiveSizes.size(); i++) {
        if (test_case.arrival_cycles[i] == static_cast<int>(cycle)) {
          enqueue_packet(fixture, kDelayedInteractiveSizes[i], TrafficHint::Interactive, false);
        }
      }

      auto storm_payload = static_cast<size_t>(70 + cycle * 9);
      auto storm_hint = test_case.storm_hints[cycle];
      enqueue_packet(fixture, storm_payload, storm_hint, storm_hint == TrafficHint::Keepalive);

      if (static_cast<int>(cycle) == test_case.override_cycle) {
        writes_before_override = fixture.inner->write_calls;
        fixture.decorator->set_max_tls_record_size(kOverrideTarget);
      }

      auto wakeup = fixture.decorator->get_shaping_wakeup();
      if (wakeup > fixture.clock->now()) {
        fixture.clock->advance(wakeup - fixture.clock->now());
      }

      fixture.inner->writes_per_flush_budget_result = test_case.budgets[cycle];
      fixture.decorator->pre_flush_write(fixture.clock->now());
    }

    for (size_t guard = 0; guard < 12; guard++) {
      auto wakeup = fixture.decorator->get_shaping_wakeup();
      if (wakeup > fixture.clock->now()) {
        fixture.clock->advance(wakeup - fixture.clock->now());
      }

      enqueue_packet(fixture, static_cast<size_t>(180 + guard * 5),
                     (guard % 2 == 0) ? TrafficHint::AuthHandshake : TrafficHint::Keepalive, (guard % 2) == 1);
      fixture.inner->writes_per_flush_budget_result = 1;
      fixture.decorator->pre_flush_write(fixture.clock->now());
    }

    for (size_t guard = 0; guard < 12; guard++) {
      auto wakeup = fixture.decorator->get_shaping_wakeup();
      if (wakeup == 0.0) {
        break;
      }
      if (wakeup > fixture.clock->now()) {
        fixture.clock->advance(wakeup - fixture.clock->now());
      }
      fixture.inner->writes_per_flush_budget_result = 1;
      fixture.decorator->pre_flush_write(fixture.clock->now());
    }

    assert_payload_order(fixture.inner->written_payloads, kDelayedInteractiveSizes);
    ASSERT_TRUE(fixture.inner->write_calls > static_cast<int>(writes_before_override));
    ASSERT_TRUE(
        all_targets_from_index_are(fixture.inner->per_write_padding_targets, writes_before_override, kOverrideTarget));
  }
}

}  // namespace