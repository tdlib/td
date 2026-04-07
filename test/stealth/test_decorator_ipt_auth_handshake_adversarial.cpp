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
using td::mtproto::test::assert_immediate_or_overdue_wakeup;
using td::mtproto::test::assert_immediate_wakeup;
using td::mtproto::test::enqueue_ipt_packet;
using td::mtproto::test::leave_delayed_interactive_queued;
using td::mtproto::test::make_decorator_ipt_fixture;

TEST(DecoratorIptAuthHandshakeAdversarial, AuthHandshakeStormDoesNotStarveReadyInteractiveAcrossSingleWriteFlushes) {
  auto fixture = make_decorator_ipt_fixture(6, 5, 1);

  auto wakeup = leave_delayed_interactive_queued(fixture, 19, 21);
  enqueue_ipt_packet(*fixture.decorator, 41, TrafficHint::AuthHandshake, false);
  enqueue_ipt_packet(*fixture.decorator, 43, TrafficHint::Keepalive, true);
  enqueue_ipt_packet(*fixture.decorator, 45, TrafficHint::BulkData, false);
  enqueue_ipt_packet(*fixture.decorator, 47, TrafficHint::AuthHandshake, false);

  fixture.clock->advance(wakeup - fixture.clock->now());

  fixture.inner->writes_per_flush_budget_result = 1;
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(TrafficHint::AuthHandshake, fixture.inner->queued_hints[1]);
  assert_immediate_wakeup(fixture);

  fixture.inner->writes_per_flush_budget_result = 1;
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(3, fixture.inner->write_calls);
  ASSERT_EQ(TrafficHint::Interactive, fixture.inner->queued_hints[2]);
  ASSERT_EQ(td::string(21, 'x'), fixture.inner->written_payloads[2]);
  assert_immediate_or_overdue_wakeup(fixture);
}

TEST(DecoratorIptAuthHandshakeAdversarial,
     NotYetReadyInteractiveDoesNotJumpAheadOfMixedBypassStormUnderSingleWriteBudget) {
  auto fixture = make_decorator_ipt_fixture();

  auto wakeup = leave_delayed_interactive_queued(fixture, 23, 25);
  enqueue_ipt_packet(*fixture.decorator, 51, TrafficHint::AuthHandshake, false);
  enqueue_ipt_packet(*fixture.decorator, 53, TrafficHint::Keepalive, true);
  enqueue_ipt_packet(*fixture.decorator, 55, TrafficHint::BulkData, false);

  fixture.inner->writes_per_flush_budget_result = 1;
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(TrafficHint::AuthHandshake, fixture.inner->queued_hints[1]);
  ASSERT_EQ(wakeup, fixture.decorator->get_shaping_wakeup());

  fixture.inner->writes_per_flush_budget_result = 2;
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(4, fixture.inner->write_calls);
  ASSERT_EQ(TrafficHint::Keepalive, fixture.inner->queued_hints[2]);
  ASSERT_EQ(TrafficHint::BulkData, fixture.inner->queued_hints[3]);
  ASSERT_EQ(wakeup, fixture.decorator->get_shaping_wakeup());
}

}  // namespace