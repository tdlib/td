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

#include <vector>

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

class BlockAfterOneWriteTransport final : public IStreamTransport {
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
    payloads.push_back(message.as_buffer_slice().as_slice().str());
    quick_acks.push_back(quick_ack);
    hints.push_back(last_hint);
    if (write_calls >= 1) {
      can_write_result = false;
    }
  }

  bool can_read() const final {
    return false;
  }

  bool can_write() const final {
    return can_write_result;
  }

  void init(td::ChainBufferReader *input, td::ChainBufferWriter *output) final {
    input_ = input;
    output_ = output;
  }

  size_t max_prepend_size() const final {
    return 32;
  }

  size_t max_append_size() const final {
    return 8;
  }

  TransportType get_type() const final {
    return TransportType{TransportType::ObfuscatedTcp, 0, ProxySecret()};
  }

  bool use_random_padding() const final {
    return false;
  }

  void pre_flush_write(double now) final {
    pre_flush_write_calls++;
    last_pre_flush_now = now;
  }

  void set_traffic_hint(TrafficHint hint) final {
    last_hint = hint;
  }

  bool supports_tls_record_sizing() const final {
    return false;
  }

  mutable bool can_write_result{true};
  td::ChainBufferReader *input_{nullptr};
  td::ChainBufferWriter *output_{nullptr};
  int write_calls{0};
  int pre_flush_write_calls{0};
  double last_pre_flush_now{0.0};
  TrafficHint last_hint{TrafficHint::Unknown};
  std::vector<td::string> payloads;
  std::vector<bool> quick_acks;
  std::vector<TrafficHint> hints;
};

struct DecoratorFixture final {
  td::unique_ptr<StealthTransportDecorator> decorator;
  BlockAfterOneWriteTransport *inner{nullptr};
  MockClock *clock{nullptr};
};

DecoratorFixture make_test_decorator() {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.ring_capacity = 4;
  config.high_watermark = 3;
  config.low_watermark = 1;

  auto inner = td::make_unique<BlockAfterOneWriteTransport>();
  auto *inner_ptr = inner.get();
  auto clock = td::make_unique<MockClock>();
  auto *clock_ptr = clock.get();
  auto decorator =
      StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(7), std::move(clock));
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr, clock_ptr};
}

TEST(DecoratorPartialDrainAdversarial, MidBatchInnerBackpressurePreservesRemainingQueueOrderAndHints) {
  auto fixture = make_test_decorator();

  fixture.decorator->set_traffic_hint(TrafficHint::Keepalive);
  fixture.decorator->write(make_test_buffer(17), false);
  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer(19), true);

  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(1u, fixture.inner->payloads.size());
  ASSERT_EQ(td::string(17, 'x'), fixture.inner->payloads[0]);
  ASSERT_EQ(1u, fixture.inner->hints.size());
  ASSERT_EQ(TrafficHint::Keepalive, fixture.inner->hints[0]);
  ASSERT_EQ(fixture.clock->now(), fixture.decorator->get_shaping_wakeup());

  fixture.inner->can_write_result = true;
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(2u, fixture.inner->payloads.size());
  ASSERT_EQ(td::string(19, 'x'), fixture.inner->payloads[1]);
  ASSERT_EQ(2u, fixture.inner->hints.size());
  ASSERT_EQ(TrafficHint::BulkData, fixture.inner->hints[1]);
  ASSERT_EQ(0.0, fixture.decorator->get_shaping_wakeup());
}

}  // namespace
