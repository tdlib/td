//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/DecoratorIptInvariantHelper.h"

#include "td/utils/tests.h"

#include <array>

namespace {

using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::any_payload_written;
using td::mtproto::test::assert_first_future_deadline_wakeup;
using td::mtproto::test::assert_immediate_or_deadline_window_wakeup;
using td::mtproto::test::assert_immediate_or_overdue_wakeup;
using td::mtproto::test::assert_payload_order;
using td::mtproto::test::drain_ipt_until_empty;
using td::mtproto::test::drain_ipt_until_future_wakeup;
using td::mtproto::test::enqueue_ipt_packet;
using td::mtproto::test::IptScheduleRng;
using td::mtproto::test::leave_delayed_interactive_queued;
using td::mtproto::test::make_decorator_ipt_fixture;

TEST(DecoratorIptStormSoak, SeededStormScheduleFlushesStaggeredInteractivePacketsInArrivalOrder) {
  auto fixture = make_decorator_ipt_fixture();
  IptScheduleRng schedule(0xC0FFEE11u);

  constexpr std::array<int, 6> kArrivalCycles = {0, 1, 3, 5, 7, 10};
  constexpr std::array<size_t, 6> kInteractiveSizes = {31, 33, 35, 37, 39, 41};

  bool interactive_flushed_before_final_drain = false;

  for (int cycle = 0; cycle < 14; cycle++) {
    for (size_t i = 0; i < kArrivalCycles.size(); i++) {
      if (kArrivalCycles[i] == cycle) {
        enqueue_ipt_packet(*fixture.decorator, kInteractiveSizes[i], TrafficHint::Interactive, false);
      }
    }

    auto storm_size = schedule.next_storm_size();
    for (int i = 0; i < storm_size; i++) {
      auto hint = (i % 2 == 0) ? TrafficHint::Keepalive : TrafficHint::BulkData;
      enqueue_ipt_packet(*fixture.decorator, static_cast<size_t>(60 + cycle * 4 + i), hint,
                         hint == TrafficHint::Keepalive);
    }

    fixture.inner->writes_per_flush_budget_result = schedule.next_budget();
    fixture.decorator->pre_flush_write(fixture.clock->now());
    interactive_flushed_before_final_drain = interactive_flushed_before_final_drain ||
                                             any_payload_written(fixture.inner->written_payloads, kInteractiveSizes);
    fixture.clock->advance(0.02);
  }

  ASSERT_TRUE(interactive_flushed_before_final_drain);
  drain_ipt_until_empty(fixture, 64, 32);
  assert_payload_order(fixture.inner->written_payloads, kInteractiveSizes);
}

TEST(DecoratorIptStormSoak, EarliestOutstandingDeadlineReappearsAfterImmediatePressureClearsAcrossCycles) {
  auto fixture = make_decorator_ipt_fixture();
  IptScheduleRng schedule(0x51A7E123u);

  auto deadline_a = leave_delayed_interactive_queued(fixture, 19, 21);
  fixture.clock->advance(0.015);

  enqueue_ipt_packet(*fixture.decorator, 23, TrafficHint::Interactive, false);
  fixture.clock->advance(0.015);

  enqueue_ipt_packet(*fixture.decorator, 25, TrafficHint::Interactive, false);
  auto deadline_c = fixture.clock->now() + 0.05;

  for (int cycle = 0; cycle < 2; cycle++) {
    auto storm_size = 2 + (schedule.next_storm_size() % 3);
    for (int i = 0; i < storm_size; i++) {
      auto hint = (i % 2 == 0) ? TrafficHint::Keepalive : TrafficHint::BulkData;
      enqueue_ipt_packet(*fixture.decorator, static_cast<size_t>(80 + cycle * 4 + i), hint,
                         hint == TrafficHint::Keepalive);
    }

    assert_immediate_or_overdue_wakeup(fixture);

    fixture.inner->writes_per_flush_budget_result = 1 + (schedule.next_budget() % 3);
    fixture.decorator->pre_flush_write(fixture.clock->now());

    assert_immediate_or_deadline_window_wakeup(fixture, std::array<double, 2>{deadline_a, deadline_c});

    fixture.clock->advance(0.02);
  }

  drain_ipt_until_future_wakeup(fixture);
  assert_first_future_deadline_wakeup(fixture, std::array<double, 2>{deadline_a, deadline_c});
}

}  // namespace