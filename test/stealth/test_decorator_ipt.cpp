//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/DecoratorIptInvariantHelper.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::assert_immediate_wakeup;
using td::mtproto::test::enqueue_ipt_packet;
using td::mtproto::test::make_decorator_ipt_fixture;
using td::mtproto::test::make_ipt_test_buffer;

TEST(DecoratorIpt, QueuedInteractiveTrafficWaitsUntilComputedDeadline) {
  auto fixture = make_decorator_ipt_fixture();

  enqueue_ipt_packet(*fixture.decorator, 29, TrafficHint::Interactive, false);
  enqueue_ipt_packet(*fixture.decorator, 31, TrafficHint::Interactive, false);

  assert_immediate_wakeup(fixture);

  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(1u, fixture.inner->queued_hints.size());
  ASSERT_EQ(TrafficHint::Interactive, fixture.inner->queued_hints[0]);
  ASSERT_EQ(td::string(29, 'x'), fixture.inner->written_payloads[0]);

  const auto wakeup = fixture.decorator->get_shaping_wakeup();
  ASSERT_TRUE(wakeup > fixture.clock->now());

  fixture.decorator->pre_flush_write(wakeup - 0.001);
  ASSERT_EQ(1, fixture.inner->write_calls);

  fixture.decorator->pre_flush_write(wakeup);
  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(2u, fixture.inner->queued_hints.size());
  ASSERT_EQ(TrafficHint::Interactive, fixture.inner->queued_hints[1]);
  ASSERT_EQ(td::string(31, 'x'), fixture.inner->written_payloads[1]);
}

TEST(DecoratorIpt, UnknownTrafficUsesInteractivePolicyBeforeClassifierExistsWhenQueueAlreadyHasPendingData) {
  auto fixture = make_decorator_ipt_fixture();

  fixture.decorator->write(make_ipt_test_buffer(29), false);
  fixture.decorator->write(make_ipt_test_buffer(31), false);

  assert_immediate_wakeup(fixture);
  fixture.decorator->pre_flush_write(fixture.clock->now());

  const auto wakeup = fixture.decorator->get_shaping_wakeup();
  ASSERT_TRUE(wakeup > fixture.clock->now());
  fixture.decorator->pre_flush_write(wakeup);

  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(2u, fixture.inner->queued_hints.size());
  ASSERT_EQ(TrafficHint::Unknown, fixture.inner->queued_hints[0]);
  ASSERT_EQ(TrafficHint::Unknown, fixture.inner->queued_hints[1]);
  ASSERT_EQ(td::string(29, 'x'), fixture.inner->written_payloads[0]);
  ASSERT_EQ(td::string(31, 'x'), fixture.inner->written_payloads[1]);
}

TEST(DecoratorIpt, KeepaliveBypassesDelayWhileInteractiveBurstRemainsQueued) {
  auto fixture = make_decorator_ipt_fixture();

  enqueue_ipt_packet(*fixture.decorator, 37, TrafficHint::Interactive, false);
  enqueue_ipt_packet(*fixture.decorator, 39, TrafficHint::Interactive, false);

  assert_immediate_wakeup(fixture);
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(1u, fixture.inner->queued_hints.size());
  ASSERT_EQ(TrafficHint::Interactive, fixture.inner->queued_hints[0]);
  ASSERT_EQ(td::string(37, 'x'), fixture.inner->written_payloads[0]);

  auto burst_wakeup = fixture.decorator->get_shaping_wakeup();
  ASSERT_TRUE(burst_wakeup > fixture.clock->now());

  enqueue_ipt_packet(*fixture.decorator, 41, TrafficHint::Keepalive, true);
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(2u, fixture.inner->queued_hints.size());
  ASSERT_EQ(TrafficHint::Keepalive, fixture.inner->queued_hints[1]);
  ASSERT_EQ(td::string(41, 'x'), fixture.inner->written_payloads[1]);

  fixture.decorator->pre_flush_write(burst_wakeup);
  ASSERT_EQ(3, fixture.inner->write_calls);
  ASSERT_EQ(3u, fixture.inner->queued_hints.size());
  ASSERT_EQ(TrafficHint::Interactive, fixture.inner->queued_hints[2]);
  ASSERT_EQ(td::string(39, 'x'), fixture.inner->written_payloads[2]);
}

TEST(DecoratorIpt, BlockedInteractiveFlushPreservesDeadlineUntilInnerCanWrite) {
  auto fixture = make_decorator_ipt_fixture();

  enqueue_ipt_packet(*fixture.decorator, 43, TrafficHint::Interactive, false);
  auto wakeup = fixture.decorator->get_shaping_wakeup();

  fixture.inner->can_write_result = false;
  fixture.decorator->pre_flush_write(wakeup);
  ASSERT_EQ(0, fixture.inner->write_calls);
  ASSERT_EQ(wakeup, fixture.decorator->get_shaping_wakeup());

  fixture.inner->can_write_result = true;
  fixture.decorator->pre_flush_write(wakeup);
  ASSERT_EQ(1, fixture.inner->write_calls);
}

}  // namespace