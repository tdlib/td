//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/MockClock.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/RecordingTransport.h"

#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"

#include "td/utils/buffer.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;

td::BufferWriter make_test_buffer(size_t size) {
  return td::BufferWriter(td::Slice(td::string(size, 'x')), 32, 0);
}

struct DecoratorFixture final {
  td::unique_ptr<StealthTransportDecorator> decorator;
  RecordingTransport *inner{nullptr};
  MockClock *clock{nullptr};
};

DecoratorFixture make_test_decorator(size_t capacity = 8, size_t high = 6, size_t low = 2,
                                     bool supports_tls_record_sizing = true) {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.ring_capacity = capacity;
  config.high_watermark = high;
  config.low_watermark = low;

  auto inner = td::make_unique<RecordingTransport>();
  inner->supports_tls_record_sizing_result = supports_tls_record_sizing;
  auto *inner_ptr = inner.get();
  auto clock = td::make_unique<MockClock>();
  auto *clock_ptr = clock.get();
  auto decorator =
      StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(7), std::move(clock));
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr, clock_ptr};
}

void enqueue_n(StealthTransportDecorator &decorator, size_t count) {
  for (size_t i = 0; i < count; i++) {
    decorator.set_traffic_hint(TrafficHint::BulkData);
    decorator.write(make_test_buffer(32 + i), false);
  }
}

void enqueue_with_clock_step(DecoratorFixture &fixture, size_t payload_size, double advance_seconds = 0.0,
                             TrafficHint hint = TrafficHint::BulkData) {
  fixture.decorator->set_traffic_hint(hint);
  fixture.decorator->write(make_test_buffer(payload_size), false);
  if (advance_seconds > 0.0) {
    fixture.clock->advance(advance_seconds);
  }
}

TEST(DecoratorContract, DelegatesReadPathToInnerTransport) {
  auto fixture = make_test_decorator();
  td::BufferSlice message;
  td::uint32 quick_ack = 0;

  auto result = fixture.decorator->read_next(&message, &quick_ack);
  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(fixture.inner->next_read_message.size(), result.ok());
  ASSERT_EQ(fixture.inner->support_quick_ack(), fixture.decorator->support_quick_ack());
  ASSERT_EQ(fixture.inner->max_prepend_size(), fixture.decorator->max_prepend_size());
  ASSERT_EQ(fixture.inner->max_append_size(), fixture.decorator->max_append_size());
  ASSERT_EQ(1, fixture.inner->read_next_calls);
}

TEST(DecoratorHint, ConsumeOnceHintResetsToUnknown) {
  auto fixture = make_test_decorator();
  fixture.decorator->set_traffic_hint(TrafficHint::Keepalive);
  fixture.decorator->write(make_test_buffer(64), false);
  fixture.decorator->write(make_test_buffer(64), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());
  auto wakeup_at = fixture.decorator->get_shaping_wakeup();
  ASSERT_TRUE(wakeup_at > fixture.clock->now());
  fixture.decorator->pre_flush_write(wakeup_at);

  ASSERT_EQ(2u, fixture.inner->queued_hints.size());
  ASSERT_EQ(TrafficHint::Keepalive, fixture.inner->queued_hints[0]);
  ASSERT_EQ(TrafficHint::Unknown, fixture.inner->queued_hints[1]);
}

TEST(DecoratorBackpressure, LatchesAtHighAndReleasesAtLow) {
  auto fixture = make_test_decorator(8, 6, 2);
  enqueue_n(*fixture.decorator, 6);
  ASSERT_FALSE(fixture.decorator->can_write());

  fixture.inner->can_write_result = false;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_FALSE(fixture.decorator->can_write());

  fixture.inner->can_write_result = true;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_TRUE(fixture.decorator->can_write());
  ASSERT_EQ(6, fixture.inner->write_calls);
}

TEST(DecoratorBackpressure, DeferredFlushPreservesQueuedPayloadsAndFlags) {
  auto fixture = make_test_decorator(8, 6, 2);

  fixture.decorator->set_traffic_hint(TrafficHint::Keepalive);
  fixture.decorator->write(make_test_buffer(31), true);
  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer(47), false);

  fixture.inner->can_write_result = false;
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(0, fixture.inner->write_calls);
  ASSERT_TRUE(fixture.inner->written_payloads.empty());

  fixture.inner->can_write_result = true;
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(2u, fixture.inner->written_payloads.size());
  ASSERT_EQ(td::string(31, 'x'), fixture.inner->written_payloads[0]);
  ASSERT_EQ(td::string(47, 'x'), fixture.inner->written_payloads[1]);
  ASSERT_EQ(2u, fixture.inner->written_quick_acks.size());
  ASSERT_TRUE(fixture.inner->written_quick_acks[0]);
  ASSERT_FALSE(fixture.inner->written_quick_acks[1]);
  ASSERT_EQ(2u, fixture.inner->queued_hints.size());
  ASSERT_EQ(TrafficHint::Keepalive, fixture.inner->queued_hints[0]);
  ASSERT_EQ(TrafficHint::BulkData, fixture.inner->queued_hints[1]);
}

TEST(DecoratorBackpressure, FailedFlushKeepsWakeupAndQueuedOrderStable) {
  auto fixture = make_test_decorator(8, 6, 2);

  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer(19), false);
  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer(23), false);
  auto wakeup_at = fixture.decorator->get_shaping_wakeup();

  fixture.inner->can_write_result = false;
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(wakeup_at, fixture.decorator->get_shaping_wakeup());

  fixture.inner->can_write_result = true;
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(2u, fixture.inner->written_payloads.size());
  ASSERT_EQ(td::string(19, 'x'), fixture.inner->written_payloads[0]);
  ASSERT_EQ(td::string(23, 'x'), fixture.inner->written_payloads[1]);
}

TEST(DecoratorBackpressure, RepeatedCanWriteAbuseKeepsExactCapacityOrderingStable) {
  auto fixture = make_test_decorator(4, 3, 1);

  enqueue_with_clock_step(fixture, 41);
  enqueue_with_clock_step(fixture, 43);
  enqueue_with_clock_step(fixture, 47);
  ASSERT_FALSE(fixture.decorator->can_write());

  for (int i = 0; i < 8; i++) {
    ASSERT_FALSE(fixture.decorator->can_write());
  }

  enqueue_with_clock_step(fixture, 53);
  ASSERT_FALSE(fixture.decorator->can_write());

  fixture.inner->can_write_result = false;
  for (int i = 0; i < 3; i++) {
    fixture.decorator->pre_flush_write(fixture.clock->now());
    ASSERT_FALSE(fixture.decorator->can_write());
    ASSERT_EQ(0, fixture.inner->write_calls);
  }

  fixture.inner->can_write_result = true;
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(4, fixture.inner->write_calls);
  ASSERT_TRUE(fixture.decorator->can_write());
  ASSERT_EQ(4u, fixture.inner->written_payloads.size());
  ASSERT_EQ(td::string(41, 'x'), fixture.inner->written_payloads[0]);
  ASSERT_EQ(td::string(43, 'x'), fixture.inner->written_payloads[1]);
  ASSERT_EQ(td::string(47, 'x'), fixture.inner->written_payloads[2]);
  ASSERT_EQ(td::string(53, 'x'), fixture.inner->written_payloads[3]);
}

TEST(DecoratorBackpressure, ExactCapacityDrainKeepsBackpressureLatchedAboveLowWatermark) {
  auto fixture = make_test_decorator(4, 3, 1);

  enqueue_with_clock_step(fixture, 37, 0.100);
  enqueue_with_clock_step(fixture, 39, 0.100);
  enqueue_with_clock_step(fixture, 41, 0.100);
  enqueue_with_clock_step(fixture, 43, 0.100);

  ASSERT_FALSE(fixture.decorator->can_write());

  fixture.decorator->pre_flush_write(1000.150);
  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_FALSE(fixture.decorator->can_write());
  ASSERT_EQ(2u, fixture.inner->written_payloads.size());
  ASSERT_EQ(td::string(37, 'x'), fixture.inner->written_payloads[0]);
  ASSERT_EQ(td::string(39, 'x'), fixture.inner->written_payloads[1]);

  fixture.decorator->pre_flush_write(1000.350);
  ASSERT_EQ(4, fixture.inner->write_calls);
  ASSERT_TRUE(fixture.decorator->can_write());
  ASSERT_EQ(4u, fixture.inner->written_payloads.size());
  ASSERT_EQ(td::string(41, 'x'), fixture.inner->written_payloads[2]);
  ASSERT_EQ(td::string(43, 'x'), fixture.inner->written_payloads[3]);
}

TEST(DecoratorWakeup, ReturnsEarliestDeadlineFromRing) {
  auto fixture = make_test_decorator();
  fixture.decorator->set_traffic_hint(TrafficHint::Interactive);
  fixture.decorator->write(make_test_buffer(256), false);
  ASSERT_EQ(fixture.clock->now(), fixture.decorator->get_shaping_wakeup());
}

TEST(DecoratorWakeup, DeadlineProgressionIsMonotonicAcrossPartialDrains) {
  auto fixture = make_test_decorator(4, 3, 1);

  auto first_deadline = fixture.clock->now();
  enqueue_with_clock_step(fixture, 29, 0.100, TrafficHint::Keepalive);
  auto second_deadline = fixture.clock->now();
  enqueue_with_clock_step(fixture, 31, 0.200, TrafficHint::BulkData);
  auto third_deadline = fixture.clock->now();
  enqueue_with_clock_step(fixture, 33, 0.0, TrafficHint::AuthHandshake);

  ASSERT_EQ(first_deadline, fixture.decorator->get_shaping_wakeup());

  fixture.decorator->pre_flush_write(first_deadline - 0.001);
  ASSERT_EQ(0, fixture.inner->write_calls);
  ASSERT_EQ(first_deadline, fixture.decorator->get_shaping_wakeup());

  fixture.decorator->pre_flush_write(first_deadline);
  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(second_deadline, fixture.decorator->get_shaping_wakeup());

  fixture.decorator->pre_flush_write(second_deadline);
  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(third_deadline, fixture.decorator->get_shaping_wakeup());

  fixture.decorator->pre_flush_write(third_deadline);
  ASSERT_EQ(3, fixture.inner->write_calls);
  ASSERT_EQ(0.0, fixture.decorator->get_shaping_wakeup());
}

TEST(DecoratorContract, PreFlushDelegatesToInnerTransport) {
  auto fixture = make_test_decorator();

  fixture.decorator->pre_flush_write(1234.5);

  ASSERT_EQ(1, fixture.inner->pre_flush_write_calls);
  ASSERT_EQ(1234.5, fixture.inner->last_pre_flush_now);
}

TEST(DecoratorWakeup, EmptyQueueStillPropagatesInnerWakeup) {
  auto fixture = make_test_decorator();
  fixture.inner->shaping_wakeup_result = 77.25;

  ASSERT_EQ(77.25, fixture.decorator->get_shaping_wakeup());
}

TEST(DecoratorWakeup, ReturnsEarliestOfQueueAndInnerWakeups) {
  auto fixture = make_test_decorator();
  fixture.inner->shaping_wakeup_result = fixture.clock->now() + 10.0;

  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer(64), false);
  ASSERT_EQ(fixture.clock->now(), fixture.decorator->get_shaping_wakeup());

  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(fixture.inner->shaping_wakeup_result, fixture.decorator->get_shaping_wakeup());

  fixture.clock->advance(1.0);
  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer(96), false);
  fixture.inner->shaping_wakeup_result = fixture.clock->now() - 0.25;
  ASSERT_EQ(fixture.inner->shaping_wakeup_result, fixture.decorator->get_shaping_wakeup());
}

TEST(DecoratorRecordSizing, ExplicitRuntimeOverridePersistsAcrossFlushes) {
  auto fixture = make_test_decorator();
  ASSERT_FALSE(fixture.inner->max_tls_record_sizes.empty());

  auto initial_sample = fixture.inner->max_tls_record_sizes.back();
  ASSERT_TRUE(initial_sample >= 1200);
  ASSERT_TRUE(initial_sample <= 1460);

  fixture.decorator->set_max_tls_record_size(4096);
  ASSERT_EQ(4096, fixture.inner->max_tls_record_sizes.back());

  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer(67), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(4096, fixture.inner->max_tls_record_sizes.back());

  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer(79), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(4096, fixture.inner->max_tls_record_sizes.back());
}

TEST(DecoratorRecordSizing, ExplicitRuntimeOverrideRespectsInnerCapabilityGuard) {
  auto fixture = make_test_decorator(8, 6, 2, false);
  ASSERT_TRUE(fixture.inner->max_tls_record_sizes.empty());
  ASSERT_FALSE(fixture.decorator->supports_tls_record_sizing());

  fixture.decorator->set_max_tls_record_size(4096);
  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer(67), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_TRUE(fixture.inner->max_tls_record_sizes.empty());
}

TEST(DecoratorSafety, QueuedWritesNeverBypassInnerWritePathBeforeFlush) {
  auto fixture = make_test_decorator(3, 2, 1);
  enqueue_n(*fixture.decorator, 2);

  ASSERT_EQ(0, fixture.inner->write_calls);
  ASSERT_FALSE(fixture.decorator->can_write());

  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(2, fixture.inner->write_calls);
}

}  // namespace