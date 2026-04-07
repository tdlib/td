//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/MockClock.h"
#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"

#include "td/utils/buffer.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::IStreamTransport;
using td::mtproto::ProxySecret;
using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::TransportType;

DrsPhaseModel make_phase(td::int32 cap) {
  DrsPhaseModel phase;
  phase.bins = {{cap, cap, 1}};
  phase.max_repeat_run = 1;
  phase.local_jitter = 0;
  return phase;
}

td::BufferWriter make_test_buffer(size_t size) {
  return td::BufferWriter(td::Slice(td::string(size, 'x')), 32, 0);
}

class TransientFlushOverheadTransport final : public IStreamTransport {
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
    overhead_consumed_for_active_flush_ = true;
    completed_flushes_++;
  }

  bool can_read() const final {
    return false;
  }

  bool can_write() const final {
    overhead_consumed_for_active_flush_ = false;
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

  bool supports_tls_record_sizing() const final {
    return true;
  }

  td::int32 tls_record_sizing_payload_overhead() const final {
    if (overhead_consumed_for_active_flush_) {
      return 0;
    }
    if (completed_flushes_ >= static_cast<int>(flush_payload_overheads.size())) {
      return 0;
    }
    return flush_payload_overheads[completed_flushes_];
  }

  td::ChainBufferReader *input_{nullptr};
  td::ChainBufferWriter *output_{nullptr};
  int write_calls{0};
  TrafficHint last_hint{TrafficHint::Unknown};
  std::vector<bool> written_quick_acks;
  std::vector<td::string> written_payloads;
  std::vector<td::int32> max_tls_record_sizes;
  std::vector<td::int32> flush_payload_overheads;

 private:
  mutable bool overhead_consumed_for_active_flush_{false};
  mutable int completed_flushes_{0};
};

struct DecoratorFixture final {
  td::unique_ptr<StealthTransportDecorator> decorator;
  TransientFlushOverheadTransport *inner{nullptr};
  MockClock *clock{nullptr};
};

DecoratorFixture make_test_decorator(std::vector<td::int32> flush_payload_overheads) {
  MockRng rng(91);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_phase(900);
  config.drs_policy.congestion_open = make_phase(1400);
  config.drs_policy.steady_state = make_phase(2400);
  config.drs_policy.slow_start_records = 1;
  config.drs_policy.congestion_bytes = 1000;
  config.drs_policy.idle_reset_ms_min = 1000;
  config.drs_policy.idle_reset_ms_max = 1000;
  config.drs_policy.min_payload_cap = 900;
  config.drs_policy.max_payload_cap = 2400;

  auto inner = td::make_unique<TransientFlushOverheadTransport>();
  inner->flush_payload_overheads = std::move(flush_payload_overheads);
  auto *inner_ptr = inner.get();
  auto clock = td::make_unique<MockClock>();
  auto *clock_ptr = clock.get();
  auto decorator =
      StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(92), std::move(clock));
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr, clock_ptr};
}

TEST(DecoratorDrsFirstWriteOverheadAdversarial, TransientFlushOverheadAdvancesCongestionPhaseUsingPreWriteBytes) {
  auto fixture = make_test_decorator({0, 600, 0});
  ASSERT_FALSE(fixture.inner->max_tls_record_sizes.empty());

  fixture.decorator->set_traffic_hint(TrafficHint::Interactive);
  fixture.decorator->write(make_test_buffer(64), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(900, fixture.inner->max_tls_record_sizes.back());

  fixture.decorator->set_traffic_hint(TrafficHint::Interactive);
  fixture.decorator->write(make_test_buffer(500), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(2000, fixture.inner->max_tls_record_sizes.back());

  fixture.decorator->set_traffic_hint(TrafficHint::Interactive);
  fixture.decorator->write(make_test_buffer(64), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(2400, fixture.inner->max_tls_record_sizes.back());
}

}  // namespace