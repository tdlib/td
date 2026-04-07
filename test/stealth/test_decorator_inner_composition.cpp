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

class FlushOnPreFlushTransport final : public IStreamTransport {
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
    staged_payloads.push_back(message.as_buffer_slice().as_slice().str());
    staged_quick_acks.push_back(quick_ack);
    staged_hints.push_back(last_hint);
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
    for (size_t i = 0; i < staged_payloads.size(); i++) {
      flushed_payloads.push_back(staged_payloads[i]);
      flushed_quick_acks.push_back(staged_quick_acks[i]);
      flushed_hints.push_back(staged_hints[i]);
    }
    staged_payloads.clear();
    staged_quick_acks.clear();
    staged_hints.clear();
  }

  double get_shaping_wakeup() const final {
    return shaping_wakeup_result;
  }

  void set_traffic_hint(TrafficHint hint) final {
    set_traffic_hint_calls++;
    last_hint = hint;
    seen_hints.push_back(hint);
  }

  void set_max_tls_record_size(td::int32 size) final {
    max_tls_record_sizes.push_back(size);
  }

  bool supports_tls_record_sizing() const final {
    return supports_tls_record_sizing_result;
  }

  bool can_write_result{true};
  bool supports_tls_record_sizing_result{true};
  double shaping_wakeup_result{0.0};
  td::ChainBufferReader *input_{nullptr};
  td::ChainBufferWriter *output_{nullptr};
  int write_calls{0};
  int pre_flush_write_calls{0};
  int set_traffic_hint_calls{0};
  double last_pre_flush_now{0.0};
  TrafficHint last_hint{TrafficHint::Unknown};
  std::vector<TrafficHint> seen_hints;
  std::vector<TrafficHint> staged_hints;
  std::vector<TrafficHint> flushed_hints;
  std::vector<td::string> staged_payloads;
  std::vector<td::string> flushed_payloads;
  std::vector<bool> staged_quick_acks;
  std::vector<bool> flushed_quick_acks;
  std::vector<td::int32> max_tls_record_sizes;
};

struct DecoratorFixture final {
  td::unique_ptr<StealthTransportDecorator> decorator;
  FlushOnPreFlushTransport *inner{nullptr};
  MockClock *clock{nullptr};
};

DecoratorFixture make_test_decorator() {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);

  auto inner = td::make_unique<FlushOnPreFlushTransport>();
  auto *inner_ptr = inner.get();
  auto clock = td::make_unique<MockClock>();
  auto *clock_ptr = clock.get();
  auto decorator =
      StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(7), std::move(clock));
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr, clock_ptr};
}

TEST(DecoratorInnerComposition, SingleFlushCycleDrainsDecoratorQueueBeforeInnerFlushHook) {
  auto fixture = make_test_decorator();

  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer(37), true);

  ASSERT_TRUE(fixture.inner->staged_payloads.empty());
  ASSERT_TRUE(fixture.inner->flushed_payloads.empty());

  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(1, fixture.inner->pre_flush_write_calls);
  ASSERT_EQ(fixture.clock->now(), fixture.inner->last_pre_flush_now);
  ASSERT_TRUE(fixture.inner->staged_payloads.empty());
  ASSERT_EQ(1u, fixture.inner->flushed_payloads.size());
  ASSERT_EQ(td::string(37, 'x'), fixture.inner->flushed_payloads[0]);
  ASSERT_EQ(1u, fixture.inner->flushed_quick_acks.size());
  ASSERT_TRUE(fixture.inner->flushed_quick_acks[0]);
  ASSERT_EQ(1u, fixture.inner->flushed_hints.size());
  ASSERT_EQ(TrafficHint::BulkData, fixture.inner->flushed_hints[0]);
}

TEST(DecoratorInnerComposition, BlockedDrainDoesNotLeakHintIntoInnerTransportBeforeActualWrite) {
  auto fixture = make_test_decorator();

  fixture.decorator->set_traffic_hint(TrafficHint::Keepalive);
  fixture.decorator->write(make_test_buffer(41), false);
  fixture.inner->can_write_result = false;

  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(0, fixture.inner->write_calls);
  ASSERT_EQ(1, fixture.inner->pre_flush_write_calls);
  ASSERT_EQ(0, fixture.inner->set_traffic_hint_calls);
  ASSERT_TRUE(fixture.inner->flushed_payloads.empty());
  ASSERT_TRUE(fixture.inner->seen_hints.empty());

  fixture.inner->can_write_result = true;
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(2, fixture.inner->pre_flush_write_calls);
  ASSERT_EQ(1, fixture.inner->set_traffic_hint_calls);
  ASSERT_EQ(1u, fixture.inner->seen_hints.size());
  ASSERT_EQ(TrafficHint::Keepalive, fixture.inner->seen_hints[0]);
  ASSERT_EQ(1u, fixture.inner->flushed_payloads.size());
  ASSERT_EQ(td::string(41, 'x'), fixture.inner->flushed_payloads[0]);
}

TEST(DecoratorInnerComposition, RuntimeRecordSizeOverrideAppliesToQueuedWriteAtDrainTime) {
  auto fixture = make_test_decorator();
  ASSERT_FALSE(fixture.inner->max_tls_record_sizes.empty());

  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer(59), false);
  fixture.decorator->set_max_tls_record_size(4096);
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_FALSE(fixture.inner->max_tls_record_sizes.empty());
  ASSERT_EQ(4096, fixture.inner->max_tls_record_sizes.back());
  ASSERT_EQ(1u, fixture.inner->flushed_payloads.size());
  ASSERT_EQ(td::string(59, 'x'), fixture.inner->flushed_payloads[0]);
}

}  // namespace