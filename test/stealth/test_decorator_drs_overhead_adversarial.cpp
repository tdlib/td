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

#include <limits>

namespace {

using td::mtproto::IStreamTransport;
using td::mtproto::ProxySecret;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::TransportType;

td::BufferWriter make_test_buffer(size_t size) {
  return td::BufferWriter(td::Slice(td::string(size, 'x')), 32, 0);
}

class HostileOverheadTransport final : public IStreamTransport {
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
  std::vector<bool> written_quick_acks;
  std::vector<td::string> written_payloads;
  std::vector<td::int32> max_tls_record_sizes;
  td::int32 payload_overhead_result{0};
};

struct DecoratorFixture final {
  td::unique_ptr<StealthTransportDecorator> decorator;
  HostileOverheadTransport *inner{nullptr};
  MockClock *clock{nullptr};
};

DecoratorFixture make_test_decorator(td::int32 payload_overhead) {
  MockRng rng(41);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start.bins = {{1200, 1200, 1}};
  config.drs_policy.congestion_open.bins = {{1400, 1400, 1}};
  config.drs_policy.steady_state.bins = {{2400, 2400, 1}};
  config.drs_policy.slow_start.local_jitter = 0;
  config.drs_policy.congestion_open.local_jitter = 0;
  config.drs_policy.steady_state.local_jitter = 0;

  auto inner = td::make_unique<HostileOverheadTransport>();
  inner->payload_overhead_result = payload_overhead;
  auto *inner_ptr = inner.get();
  auto clock = td::make_unique<MockClock>();
  auto *clock_ptr = clock.get();
  auto decorator =
      StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(43), std::move(clock));
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr, clock_ptr};
}

TEST(DecoratorDrsOverheadAdversarial, SaturatesHostilePositivePayloadOverheadBeforeForwardingCap) {
  auto fixture = make_test_decorator(std::numeric_limits<td::int32>::max());
  ASSERT_FALSE(fixture.inner->max_tls_record_sizes.empty());

  fixture.decorator->set_traffic_hint(TrafficHint::Interactive);
  fixture.decorator->write(make_test_buffer(17), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(16384, fixture.inner->max_tls_record_sizes.back());
}

TEST(DecoratorDrsOverheadAdversarial, IgnoresHostileNegativePayloadOverheadBeforeForwardingCap) {
  auto fixture = make_test_decorator(std::numeric_limits<td::int32>::min());
  ASSERT_FALSE(fixture.inner->max_tls_record_sizes.empty());

  fixture.decorator->set_traffic_hint(TrafficHint::Interactive);
  fixture.decorator->write(make_test_buffer(19), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(1200, fixture.inner->max_tls_record_sizes.back());
}

}  // namespace
