// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/DecoratorIptInvariantHelper.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::assert_immediate_wakeup;
using td::mtproto::test::enqueue_ipt_packet;
using td::mtproto::test::leave_delayed_interactive_queued;
using td::mtproto::test::make_decorator_ipt_fixture;

TEST(DecoratorIptWakeupFairnessAdversarial, ImmediateBypassWinsOverLaterInnerWakeupBeforeDrain) {
  auto fixture = make_decorator_ipt_fixture();
  auto delayed_interactive_wakeup = leave_delayed_interactive_queued(fixture, 29, 31);
  fixture.inner->shaping_wakeup_result = delayed_interactive_wakeup + 5.0;

  enqueue_ipt_packet(*fixture.decorator, 41, TrafficHint::Keepalive, true);

  assert_immediate_wakeup(fixture);

  fixture.inner->writes_per_flush_budget_result = 1;
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(delayed_interactive_wakeup, fixture.decorator->get_shaping_wakeup());
}

TEST(DecoratorIptWakeupFairnessAdversarial, EarlierInnerWakeupWinsAfterImmediateBypassDrainLeavesOnlyDelayedTraffic) {
  auto fixture = make_decorator_ipt_fixture();
  auto delayed_interactive_wakeup = leave_delayed_interactive_queued(fixture, 37, 39);

  enqueue_ipt_packet(*fixture.decorator, 43, TrafficHint::BulkData, false);
  fixture.inner->shaping_wakeup_result = delayed_interactive_wakeup - 0.5;

  assert_immediate_wakeup(fixture);

  fixture.inner->writes_per_flush_budget_result = 1;
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(fixture.inner->shaping_wakeup_result, fixture.decorator->get_shaping_wakeup());
}

}  // namespace