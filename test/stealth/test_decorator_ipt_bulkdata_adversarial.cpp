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

TEST(DecoratorIptBulkDataAdversarial, BulkDataBypassDoesNotConsumePendingInteractiveDeadline) {
  auto fixture = make_decorator_ipt_fixture();

  auto wakeup = leave_delayed_interactive_queued(fixture, 23, 29);
  enqueue_ipt_packet(*fixture.decorator, 41, TrafficHint::BulkData, false);

  fixture.inner->writes_per_flush_budget_result = 1;
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(TrafficHint::BulkData, fixture.inner->queued_hints[1]);
  ASSERT_EQ(td::string(41, 'x'), fixture.inner->written_payloads[1]);
  ASSERT_EQ(wakeup, fixture.decorator->get_shaping_wakeup());
}

TEST(DecoratorIptBulkDataAdversarial, BulkDataBypassLeavesNextInteractiveReadyAtOriginalWakeup) {
  auto fixture = make_decorator_ipt_fixture();

  auto wakeup = leave_delayed_interactive_queued(fixture, 31, 37);
  enqueue_ipt_packet(*fixture.decorator, 43, TrafficHint::BulkData, false);

  fixture.inner->writes_per_flush_budget_result = 1;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(wakeup, fixture.decorator->get_shaping_wakeup());

  fixture.clock->advance(wakeup - fixture.clock->now());
  assert_immediate_wakeup(fixture);
  fixture.inner->writes_per_flush_budget_result = 1;
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(3, fixture.inner->write_calls);
  ASSERT_EQ(TrafficHint::Interactive, fixture.inner->queued_hints[2]);
  ASSERT_EQ(td::string(37, 'x'), fixture.inner->written_payloads[2]);
}

}  // namespace