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
using td::mtproto::test::assert_no_wakeup;
using td::mtproto::test::enqueue_ipt_packet;
using td::mtproto::test::make_decorator_ipt_fixture;
using td::mtproto::test::make_ipt_test_buffer;

TEST(DecoratorIptIdleFastPath, FirstInteractivePacketOnEmptyQueueFlushesImmediately) {
  auto fixture = make_decorator_ipt_fixture();

  enqueue_ipt_packet(*fixture.decorator, 29, TrafficHint::Interactive, false);

  assert_immediate_wakeup(fixture);
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(1u, fixture.inner->queued_hints.size());
  ASSERT_EQ(TrafficHint::Interactive, fixture.inner->queued_hints[0]);
  ASSERT_EQ(td::string(29, 'x'), fixture.inner->written_payloads[0]);
  assert_no_wakeup(fixture);
}

TEST(DecoratorIptIdleFastPath, FirstUnknownPacketOnEmptyQueueFlushesImmediatelyButKeepsUnknownHint) {
  auto fixture = make_decorator_ipt_fixture();

  fixture.decorator->write(make_ipt_test_buffer(31), false);

  assert_immediate_wakeup(fixture);
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(1u, fixture.inner->queued_hints.size());
  ASSERT_EQ(TrafficHint::Unknown, fixture.inner->queued_hints[0]);
  ASSERT_EQ(td::string(31, 'x'), fixture.inner->written_payloads[0]);
  assert_no_wakeup(fixture);
}

TEST(DecoratorIptIdleFastPath, InteractiveFastPathRearmsAfterQueueDrainsCompletely) {
  auto fixture = make_decorator_ipt_fixture();

  enqueue_ipt_packet(*fixture.decorator, 23, TrafficHint::Interactive, false);
  fixture.decorator->pre_flush_write(fixture.clock->now());
  assert_no_wakeup(fixture);

  fixture.clock->advance(0.250);
  enqueue_ipt_packet(*fixture.decorator, 27, TrafficHint::Interactive, false);

  assert_immediate_wakeup(fixture);
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(2u, fixture.inner->queued_hints.size());
  ASSERT_EQ(TrafficHint::Interactive, fixture.inner->queued_hints[0]);
  ASSERT_EQ(TrafficHint::Interactive, fixture.inner->queued_hints[1]);
  ASSERT_EQ(td::string(23, 'x'), fixture.inner->written_payloads[0]);
  ASSERT_EQ(td::string(27, 'x'), fixture.inner->written_payloads[1]);
  assert_no_wakeup(fixture);
}

}  // namespace