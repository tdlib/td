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

TEST(DecoratorIptStormAdversarial, RepeatedChangingBudgetsEventuallyFlushOldestReadyInteractive) {
  auto fixture = make_decorator_ipt_fixture();

  auto wakeup = leave_delayed_interactive_queued(fixture, 29, 31);

  for (int i = 0; i < 6; i++) {
    enqueue_ipt_packet(*fixture.decorator, 50 + i, TrafficHint::Keepalive, true);
  }

  fixture.inner->writes_per_flush_budget_result = 1;
  fixture.decorator->pre_flush_write(wakeup);
  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(TrafficHint::Keepalive, fixture.inner->queued_hints[1]);
  assert_immediate_wakeup(fixture);

  fixture.inner->writes_per_flush_budget_result = 1;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(3, fixture.inner->write_calls);
  ASSERT_TRUE(std::find(fixture.inner->queued_hints.begin(), fixture.inner->queued_hints.end(),
                        TrafficHint::Interactive) != fixture.inner->queued_hints.end());

  fixture.inner->writes_per_flush_budget_result = 2;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_TRUE(std::find(fixture.inner->queued_hints.begin(), fixture.inner->queued_hints.end(),
                        TrafficHint::Interactive) != fixture.inner->queued_hints.end());
}

TEST(DecoratorIptStormAdversarial, MultipleInteractivePacketsPreserveRelativeOrderAcrossBudgetChanges) {
  auto fixture = make_decorator_ipt_fixture();

  auto first_wakeup = leave_delayed_interactive_queued(fixture, 19, 21);
  enqueue_ipt_packet(*fixture.decorator, 22, TrafficHint::Interactive, false);
  auto second_wakeup = fixture.decorator->get_shaping_wakeup();
  ASSERT_EQ(first_wakeup, second_wakeup);

  for (int i = 0; i < 4; i++) {
    enqueue_ipt_packet(*fixture.decorator, 60 + i, TrafficHint::BulkData, false);
  }

  fixture.inner->writes_per_flush_budget_result = 1;
  fixture.decorator->pre_flush_write(first_wakeup);
  fixture.inner->writes_per_flush_budget_result = 2;
  fixture.decorator->pre_flush_write(first_wakeup);
  fixture.inner->writes_per_flush_budget_result = 3;
  fixture.decorator->pre_flush_write(first_wakeup);

  auto first_it =
      std::find(fixture.inner->written_payloads.begin(), fixture.inner->written_payloads.end(), td::string(21, 'x'));
  auto second_it =
      std::find(fixture.inner->written_payloads.begin(), fixture.inner->written_payloads.end(), td::string(22, 'x'));
  ASSERT_TRUE(first_it != fixture.inner->written_payloads.end());
  ASSERT_TRUE(second_it != fixture.inner->written_payloads.end());
  ASSERT_TRUE(first_it < second_it);
}

TEST(DecoratorIptStormAdversarial, WakeupFallsBackToFutureDeadlineAfterImmediateStormDrains) {
  auto fixture = make_decorator_ipt_fixture();

  auto interactive_wakeup = leave_delayed_interactive_queued(fixture, 19, 25);

  for (int i = 0; i < 3; i++) {
    enqueue_ipt_packet(*fixture.decorator, 70 + i, TrafficHint::Keepalive, true);
  }

  assert_immediate_wakeup(fixture);

  fixture.inner->writes_per_flush_budget_result = 2;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  assert_immediate_wakeup(fixture);

  fixture.inner->writes_per_flush_budget_result = 2;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(interactive_wakeup, fixture.decorator->get_shaping_wakeup());
}

TEST(DecoratorIptStormAdversarial, WatermarkLatchedContentionAlternatesAcrossSingleWriteFlushes) {
  auto fixture = make_decorator_ipt_fixture(5, 4, 1);

  auto wakeup = leave_delayed_interactive_queued(fixture, 19, 21);
  enqueue_ipt_packet(*fixture.decorator, 41, TrafficHint::Keepalive, true);
  enqueue_ipt_packet(*fixture.decorator, 43, TrafficHint::BulkData, false);
  enqueue_ipt_packet(*fixture.decorator, 45, TrafficHint::Keepalive, true);

  ASSERT_FALSE(fixture.decorator->can_write());

  fixture.clock->advance(wakeup - fixture.clock->now());

  fixture.inner->writes_per_flush_budget_result = 1;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(TrafficHint::Keepalive, fixture.inner->queued_hints[1]);
  ASSERT_FALSE(fixture.decorator->can_write());
  assert_immediate_or_overdue_wakeup(fixture);

  fixture.inner->writes_per_flush_budget_result = 1;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(3, fixture.inner->write_calls);
  ASSERT_EQ(TrafficHint::Interactive, fixture.inner->queued_hints[2]);
  ASSERT_EQ(td::string(21, 'x'), fixture.inner->written_payloads[2]);
  ASSERT_FALSE(fixture.decorator->can_write());
  assert_immediate_or_overdue_wakeup(fixture);

  fixture.inner->writes_per_flush_budget_result = 2;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(5, fixture.inner->write_calls);
  assert_no_wakeup(fixture);
  ASSERT_TRUE(fixture.decorator->can_write());
}

}  // namespace