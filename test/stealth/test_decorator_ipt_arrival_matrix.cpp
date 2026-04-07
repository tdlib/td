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

template <size_t N>
void run_arrival_order_case(const std::array<int, N> &arrival_cycles, td::uint32 seed) {
  auto fixture = make_decorator_ipt_fixture();
  IptScheduleRng schedule(seed);

  bool interactive_progressed = false;
  std::array<size_t, N> payload_sizes{};
  for (size_t i = 0; i < N; i++) {
    payload_sizes[i] = 31 + i * 2;
  }

  for (int cycle = 0; cycle < 16; cycle++) {
    for (size_t i = 0; i < N; i++) {
      if (arrival_cycles[i] == cycle) {
        enqueue_ipt_packet(*fixture.decorator, payload_sizes[i], TrafficHint::Interactive, false);
      }
    }

    auto storm_size = 1 + (schedule.next_storm_size() % 5);
    for (int i = 0; i < storm_size; i++) {
      auto hint = (i % 3 == 0) ? TrafficHint::Keepalive : TrafficHint::BulkData;
      enqueue_ipt_packet(*fixture.decorator, static_cast<size_t>(80 + cycle * 5 + i), hint,
                         hint == TrafficHint::Keepalive);
    }

    fixture.inner->writes_per_flush_budget_result = schedule.next_budget();
    fixture.decorator->pre_flush_write(fixture.clock->now());
    interactive_progressed =
        interactive_progressed || any_payload_written(fixture.inner->written_payloads, payload_sizes);
    fixture.clock->advance(0.02);
  }

  ASSERT_TRUE(interactive_progressed);
  drain_ipt_until_empty(fixture);
  assert_payload_order(fixture.inner->written_payloads, payload_sizes);
}

template <size_t N>
void run_arrival_wakeup_case(const std::array<double, N> &arrival_offsets, td::uint32 seed) {
  auto fixture = make_decorator_ipt_fixture();
  IptScheduleRng schedule(seed);

  std::array<double, N> deadlines{};
  deadlines[0] = leave_delayed_interactive_queued(fixture, 19, 21);
  for (size_t i = 1; i < N; i++) {
    if (arrival_offsets[i] != 0.0) {
      fixture.clock->advance(arrival_offsets[i]);
    }
    enqueue_ipt_packet(*fixture.decorator, 21 + i * 2, TrafficHint::Interactive, false);
    deadlines[i] = fixture.clock->now() + 0.05;
  }

  for (int cycle = 0; cycle < 2; cycle++) {
    auto storm_size = 2 + (schedule.next_storm_size() % 3);
    for (int i = 0; i < storm_size; i++) {
      auto hint = (i % 2 == 0) ? TrafficHint::Keepalive : TrafficHint::BulkData;
      enqueue_ipt_packet(*fixture.decorator, static_cast<size_t>(110 + cycle * 4 + i), hint,
                         hint == TrafficHint::Keepalive);
    }

    assert_immediate_or_overdue_wakeup(fixture);
    fixture.inner->writes_per_flush_budget_result = 1 + (schedule.next_budget() % 3);
    fixture.decorator->pre_flush_write(fixture.clock->now());

    assert_immediate_or_deadline_window_wakeup(fixture, deadlines);
    fixture.clock->advance(0.02);
  }

  drain_ipt_until_future_wakeup(fixture);
  assert_first_future_deadline_wakeup(fixture, deadlines);
}

TEST(DecoratorIptArrivalMatrix, OrderingInvariantHoldsAcrossArrivalPatterns) {
  run_arrival_order_case(std::array<int, 6>{0, 2, 4, 6, 8, 11}, 0xC0FFEE11u);
  run_arrival_order_case(std::array<int, 6>{0, 1, 2, 7, 8, 12}, 0xC0FFEE11u);
  run_arrival_order_case(std::array<int, 6>{0, 3, 4, 5, 9, 13}, 0xC0FFEE11u);
}

TEST(DecoratorIptArrivalMatrix, WakeupInvariantHoldsAcrossArrivalPatterns) {
  run_arrival_wakeup_case(std::array<double, 3>{0.0, 0.015, 0.015}, 0x51A7E123u);
  run_arrival_wakeup_case(std::array<double, 3>{0.0, 0.010, 0.025}, 0x51A7E123u);
  run_arrival_wakeup_case(std::array<double, 3>{0.0, 0.020, 0.010}, 0x51A7E123u);
}

}  // namespace