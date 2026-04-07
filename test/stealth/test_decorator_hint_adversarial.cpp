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

DecoratorFixture make_test_decorator(size_t capacity = 8, size_t high = 6, size_t low = 2) {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.ring_capacity = capacity;
  config.high_watermark = high;
  config.low_watermark = low;

  auto inner = td::make_unique<RecordingTransport>();
  auto *inner_ptr = inner.get();
  auto clock = td::make_unique<MockClock>();
  auto *clock_ptr = clock.get();
  auto decorator =
      StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(7), std::move(clock));
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr, clock_ptr};
}

TEST(DecoratorHintAdversarial, LastHintWinsBeforeWriteIsQueued) {
  auto fixture = make_test_decorator();

  fixture.decorator->set_traffic_hint(TrafficHint::Keepalive);
  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->set_traffic_hint(TrafficHint::AuthHandshake);
  fixture.decorator->write(make_test_buffer(41), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(1u, fixture.inner->queued_hints.size());
  ASSERT_EQ(TrafficHint::AuthHandshake, fixture.inner->queued_hints[0]);
}

TEST(DecoratorHintAdversarial, HintDoesNotBleedIntoLaterUnknownWriteAfterBlockedFlush) {
  auto fixture = make_test_decorator();

  fixture.decorator->set_traffic_hint(TrafficHint::Keepalive);
  fixture.decorator->write(make_test_buffer(29), false);
  fixture.decorator->write(make_test_buffer(31), false);

  fixture.inner->can_write_result = false;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(0, fixture.inner->write_calls);

  fixture.inner->can_write_result = true;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  auto wakeup_at = fixture.decorator->get_shaping_wakeup();
  ASSERT_TRUE(wakeup_at > fixture.clock->now());
  fixture.decorator->pre_flush_write(wakeup_at);

  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(2u, fixture.inner->queued_hints.size());
  ASSERT_EQ(TrafficHint::Keepalive, fixture.inner->queued_hints[0]);
  ASSERT_EQ(TrafficHint::Unknown, fixture.inner->queued_hints[1]);
}

TEST(DecoratorHintAdversarial, PendingHintSurvivesIdleFlushUntilConsumed) {
  auto fixture = make_test_decorator();

  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->pre_flush_write(fixture.clock->now());
  fixture.decorator->pre_flush_write(fixture.clock->now() + 1.0);

  ASSERT_EQ(0, fixture.inner->write_calls);
  ASSERT_EQ(0u, fixture.inner->queued_hints.size());

  fixture.decorator->write(make_test_buffer(37), false);
  fixture.decorator->pre_flush_write(fixture.clock->now() + 1.0);

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(1u, fixture.inner->queued_hints.size());
  ASSERT_EQ(TrafficHint::BulkData, fixture.inner->queued_hints[0]);

  fixture.decorator->write(make_test_buffer(43), false);
  auto wakeup_at = fixture.decorator->get_shaping_wakeup();
  ASSERT_EQ(fixture.clock->now(), wakeup_at);
  fixture.decorator->pre_flush_write(wakeup_at);

  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(2u, fixture.inner->queued_hints.size());
  ASSERT_EQ(TrafficHint::Unknown, fixture.inner->queued_hints[1]);
}

}  // namespace