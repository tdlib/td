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
using td::mtproto::test::assert_ipt_time_near;
using td::mtproto::test::enqueue_ipt_packet;
using td::mtproto::test::leave_delayed_interactive_queued;
using td::mtproto::test::make_decorator_ipt_fixture;

size_t index_of_payload(const std::vector<td::string> &payloads, const td::string &payload) {
  auto it = std::find(payloads.begin(), payloads.end(), payload);
  CHECK(it != payloads.end());
  return static_cast<size_t>(it - payloads.begin());
}

TEST(DecoratorIptStormRandomized, PseudoRandomBudgetScheduleFlushesStaggeredInteractiveArrivalsInOrder) {
  auto fixture = make_decorator_ipt_fixture();

  const td::string payload_a(31, 'x');
  const td::string payload_b(33, 'x');
  const td::string payload_c(35, 'x');

  auto deadline_a = leave_delayed_interactive_queued(fixture, 29, payload_a.size());

  enqueue_ipt_packet(*fixture.decorator, 51, TrafficHint::Keepalive, true);
  enqueue_ipt_packet(*fixture.decorator, 52, TrafficHint::Keepalive, true);
  fixture.clock->advance(0.05);

  fixture.inner->writes_per_flush_budget_result = 1;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_TRUE(std::find(fixture.inner->written_payloads.begin(), fixture.inner->written_payloads.end(), payload_a) ==
              fixture.inner->written_payloads.end());

  fixture.clock->advance(0.01);
  auto deadline_b = fixture.clock->now() + 0.05;
  enqueue_ipt_packet(*fixture.decorator, payload_b.size(), TrafficHint::Interactive, false);
  enqueue_ipt_packet(*fixture.decorator, 61, TrafficHint::BulkData, false);
  enqueue_ipt_packet(*fixture.decorator, 62, TrafficHint::Keepalive, true);

  fixture.inner->writes_per_flush_budget_result = 2;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_TRUE(std::find(fixture.inner->written_payloads.begin(), fixture.inner->written_payloads.end(), payload_a) !=
              fixture.inner->written_payloads.end());
  ASSERT_TRUE(std::find(fixture.inner->written_payloads.begin(), fixture.inner->written_payloads.end(), payload_b) ==
              fixture.inner->written_payloads.end());

  fixture.clock->advance(0.05);
  fixture.inner->writes_per_flush_budget_result = 1;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_TRUE(std::find(fixture.inner->written_payloads.begin(), fixture.inner->written_payloads.end(), payload_b) ==
              fixture.inner->written_payloads.end());

  fixture.clock->advance(0.01);
  auto deadline_c = fixture.clock->now() + 0.05;
  enqueue_ipt_packet(*fixture.decorator, payload_c.size(), TrafficHint::Interactive, false);
  enqueue_ipt_packet(*fixture.decorator, 71, TrafficHint::Keepalive, true);
  enqueue_ipt_packet(*fixture.decorator, 72, TrafficHint::BulkData, false);
  enqueue_ipt_packet(*fixture.decorator, 73, TrafficHint::Keepalive, true);

  fixture.inner->writes_per_flush_budget_result = 2;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_TRUE(std::find(fixture.inner->written_payloads.begin(), fixture.inner->written_payloads.end(), payload_b) !=
              fixture.inner->written_payloads.end());
  ASSERT_TRUE(std::find(fixture.inner->written_payloads.begin(), fixture.inner->written_payloads.end(), payload_c) ==
              fixture.inner->written_payloads.end());

  fixture.clock->advance(0.05);
  fixture.inner->writes_per_flush_budget_result = 1;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_TRUE(std::find(fixture.inner->written_payloads.begin(), fixture.inner->written_payloads.end(), payload_c) ==
              fixture.inner->written_payloads.end());

  fixture.inner->writes_per_flush_budget_result = 3;
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(0.0, fixture.decorator->get_shaping_wakeup());
  auto index_a = index_of_payload(fixture.inner->written_payloads, payload_a);
  auto index_b = index_of_payload(fixture.inner->written_payloads, payload_b);
  auto index_c = index_of_payload(fixture.inner->written_payloads, payload_c);
  ASSERT_TRUE(index_a < index_b);
  ASSERT_TRUE(index_b < index_c);
  ASSERT_TRUE(deadline_a < deadline_b);
  ASSERT_TRUE(deadline_b < deadline_c);
}

TEST(DecoratorIptStormRandomized, WakeupMigratesAcrossImmediatePressureAndFutureInteractiveDeadlines) {
  auto fixture = make_decorator_ipt_fixture();

  auto deadline_a = leave_delayed_interactive_queued(fixture, 19, 21);
  fixture.clock->advance(0.02);

  enqueue_ipt_packet(*fixture.decorator, 23, TrafficHint::Interactive, false);
  auto deadline_b = fixture.clock->now() + 0.05;
  fixture.clock->advance(0.02);

  enqueue_ipt_packet(*fixture.decorator, 25, TrafficHint::Interactive, false);
  auto deadline_c = fixture.clock->now() + 0.05;
  enqueue_ipt_packet(*fixture.decorator, 81, TrafficHint::Keepalive, true);
  enqueue_ipt_packet(*fixture.decorator, 82, TrafficHint::BulkData, false);
  enqueue_ipt_packet(*fixture.decorator, 83, TrafficHint::Keepalive, true);
  enqueue_ipt_packet(*fixture.decorator, 84, TrafficHint::BulkData, false);

  assert_immediate_wakeup(fixture);

  fixture.inner->writes_per_flush_budget_result = 2;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  assert_immediate_wakeup(fixture);

  fixture.inner->writes_per_flush_budget_result = 4;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  assert_ipt_time_near(deadline_a, fixture.decorator->get_shaping_wakeup());

  fixture.clock->advance(0.01);
  enqueue_ipt_packet(*fixture.decorator, 91, TrafficHint::Keepalive, true);
  assert_immediate_wakeup(fixture);

  fixture.inner->writes_per_flush_budget_result = 2;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  assert_ipt_time_near(deadline_b, fixture.decorator->get_shaping_wakeup());

  fixture.clock->advance(0.02);
  enqueue_ipt_packet(*fixture.decorator, 92, TrafficHint::BulkData, false);
  assert_immediate_wakeup(fixture);

  fixture.inner->writes_per_flush_budget_result = 2;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  assert_ipt_time_near(deadline_c, fixture.decorator->get_shaping_wakeup());
}

}  // namespace