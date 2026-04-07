//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/DecoratorIptInvariantHelper.h"

#include "td/utils/tests.h"

#include <algorithm>

namespace {

using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::assert_immediate_or_overdue_wakeup;
using td::mtproto::test::assert_immediate_wakeup;
using td::mtproto::test::assert_no_wakeup;
using td::mtproto::test::enqueue_ipt_packet;
using td::mtproto::test::leave_delayed_interactive_queued;
using td::mtproto::test::make_decorator_ipt_fixture;

TEST(DecoratorIptFairness, ReadyInteractiveIsNotStarvedByBypassStormWhenFlushHasSpareCapacity) {
  auto fixture = make_decorator_ipt_fixture();
  fixture.inner->writes_per_flush_budget_result = 3;

  auto wakeup = leave_delayed_interactive_queued(fixture, 29, 31);

  for (int i = 0; i < 4; i++) {
    enqueue_ipt_packet(*fixture.decorator, 40 + i, TrafficHint::Keepalive, true);
  }

  fixture.decorator->pre_flush_write(wakeup);

  ASSERT_EQ(4, fixture.inner->write_calls);
  ASSERT_TRUE(std::find(fixture.inner->queued_hints.begin(), fixture.inner->queued_hints.end(),
                        TrafficHint::Interactive) != fixture.inner->queued_hints.end());
  assert_immediate_wakeup(fixture);

  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(6, fixture.inner->write_calls);
  assert_no_wakeup(fixture);
}

TEST(DecoratorIptFairness, NotYetReadyInteractiveDoesNotJumpAheadOfBypassTraffic) {
  auto fixture = make_decorator_ipt_fixture();
  fixture.inner->writes_per_flush_budget_result = 2;

  auto wakeup = leave_delayed_interactive_queued(fixture, 25, 27);

  enqueue_ipt_packet(*fixture.decorator, 41, TrafficHint::Keepalive, true);
  enqueue_ipt_packet(*fixture.decorator, 43, TrafficHint::BulkData, false);

  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(3, fixture.inner->write_calls);
  ASSERT_EQ(3u, fixture.inner->queued_hints.size());
  ASSERT_EQ(TrafficHint::Keepalive, fixture.inner->queued_hints[1]);
  ASSERT_EQ(TrafficHint::BulkData, fixture.inner->queued_hints[2]);
  ASSERT_EQ(wakeup, fixture.decorator->get_shaping_wakeup());
}

TEST(DecoratorIptFairness, ReadyInteractiveStillProgressesAtHighWatermarkUnderMixedBackpressure) {
  auto fixture = make_decorator_ipt_fixture(5, 4, 1);

  auto wakeup = leave_delayed_interactive_queued(fixture, 29, 31);
  enqueue_ipt_packet(*fixture.decorator, 41, TrafficHint::Keepalive, true);
  enqueue_ipt_packet(*fixture.decorator, 43, TrafficHint::BulkData, false);
  enqueue_ipt_packet(*fixture.decorator, 45, TrafficHint::Keepalive, true);

  ASSERT_FALSE(fixture.decorator->can_write());
  assert_immediate_wakeup(fixture);

  fixture.clock->advance(wakeup - fixture.clock->now());
  fixture.inner->writes_per_flush_budget_result = 2;
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(3, fixture.inner->write_calls);
  ASSERT_FALSE(fixture.decorator->can_write());
  ASSERT_TRUE(std::find(fixture.inner->written_payloads.begin(), fixture.inner->written_payloads.end(),
                        td::string(31, 'x')) != fixture.inner->written_payloads.end());
  ASSERT_TRUE(std::find(fixture.inner->queued_hints.begin() + 1, fixture.inner->queued_hints.end(),
                        TrafficHint::Interactive) != fixture.inner->queued_hints.end());
  assert_immediate_or_overdue_wakeup(fixture);

  fixture.inner->writes_per_flush_budget_result = 2;
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(5, fixture.inner->write_calls);
  assert_no_wakeup(fixture);
  ASSERT_TRUE(fixture.decorator->can_write());
}

}  // namespace