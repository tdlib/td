//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "test/stealth/MockClock.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/RecordingTransport.h"

#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"

#include "td/utils/buffer.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace td {
namespace mtproto {
namespace test {

inline BufferWriter make_ipt_test_buffer(size_t size) {
  return BufferWriter(Slice(string(size, 'x')), 32, 0);
}

struct DecoratorIptFixture final {
  unique_ptr<stealth::StealthTransportDecorator> decorator;
  RecordingTransport *inner{nullptr};
  MockClock *clock{nullptr};
};

inline DecoratorIptFixture make_decorator_ipt_fixture(size_t ring_capacity = 128, size_t high_watermark = 96,
                                                      size_t low_watermark = 32) {
  MockRng rng(1);
  auto config = stealth::StealthConfig::default_config(rng);
  config.ring_capacity = ring_capacity;
  config.high_watermark = high_watermark;
  config.low_watermark = low_watermark;
  config.ipt_params.burst_mu_ms = std::log(50.0);
  config.ipt_params.burst_sigma = 0.0;
  config.ipt_params.burst_max_ms = 50.0;
  config.ipt_params.idle_alpha = 2.0;
  config.ipt_params.idle_scale_ms = 10.0;
  config.ipt_params.idle_max_ms = 100.0;
  config.ipt_params.p_burst_stay = 1.0;
  config.ipt_params.p_idle_to_burst = 1.0;

  auto inner = make_unique<RecordingTransport>();
  auto *inner_ptr = inner.get();
  auto clock = make_unique<MockClock>();
  auto *clock_ptr = clock.get();
  auto decorator =
      stealth::StealthTransportDecorator::create(std::move(inner), config, make_unique<MockRng>(7), std::move(clock));
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr, clock_ptr};
}

inline void enqueue_ipt_packet(stealth::StealthTransportDecorator &decorator, size_t payload_size,
                               stealth::TrafficHint hint, bool quick_ack) {
  decorator.set_traffic_hint(hint);
  decorator.write(make_ipt_test_buffer(payload_size), quick_ack);
}

inline void assert_ipt_time_near(double expected, double actual) {
  ASSERT_TRUE(std::abs(expected - actual) < 1e-6);
}

inline void assert_immediate_wakeup(const DecoratorIptFixture &fixture) {
  assert_ipt_time_near(fixture.clock->now(), fixture.decorator->get_shaping_wakeup());
}

inline void assert_immediate_or_overdue_wakeup(const DecoratorIptFixture &fixture) {
  ASSERT_TRUE(fixture.decorator->get_shaping_wakeup() <= fixture.clock->now() + 1e-6);
}

inline void assert_no_wakeup(const DecoratorIptFixture &fixture) {
  ASSERT_EQ(0.0, fixture.decorator->get_shaping_wakeup());
}

template <size_t N>
inline void assert_immediate_or_deadline_window_wakeup(const DecoratorIptFixture &fixture,
                                                       const std::array<double, N> &deadlines) {
  static_assert(N > 0, "deadline window requires at least one deadline");
  auto wakeup = fixture.decorator->get_shaping_wakeup();
  ASSERT_TRUE(wakeup <= fixture.clock->now() + 1e-6 ||
              (wakeup >= deadlines.front() - 1e-6 && wakeup <= deadlines.back() + 1e-6));
}

inline void prime_immediate_interactive(DecoratorIptFixture &fixture, size_t payload_size = 19, int write_budget = 64) {
  auto previous_budget = fixture.inner->writes_per_flush_budget_result;
  enqueue_ipt_packet(*fixture.decorator, payload_size, stealth::TrafficHint::Interactive, false);
  assert_immediate_or_overdue_wakeup(fixture);
  fixture.inner->writes_per_flush_budget_result = write_budget;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  fixture.inner->writes_per_flush_budget_result = previous_budget;
}

inline double leave_delayed_interactive_queued(DecoratorIptFixture &fixture, size_t immediate_payload_size = 19,
                                               size_t delayed_payload_size = 21, int write_budget = 64) {
  auto previous_budget = fixture.inner->writes_per_flush_budget_result;
  enqueue_ipt_packet(*fixture.decorator, immediate_payload_size, stealth::TrafficHint::Interactive, false);
  enqueue_ipt_packet(*fixture.decorator, delayed_payload_size, stealth::TrafficHint::Interactive, false);
  assert_immediate_or_overdue_wakeup(fixture);
  fixture.inner->writes_per_flush_budget_result = write_budget;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  fixture.inner->writes_per_flush_budget_result = previous_budget;
  auto wakeup = fixture.decorator->get_shaping_wakeup();
  ASSERT_TRUE(wakeup > fixture.clock->now());
  return wakeup;
}

struct IptScheduleRng final {
  explicit IptScheduleRng(uint32 seed) : state(seed) {
  }

  uint32 next() {
    state = state * 1664525u + 1013904223u;
    return state;
  }

  int next_budget() {
    return static_cast<int>(next() % 4u);
  }

  int next_storm_size() {
    return 1 + static_cast<int>(next() % 4u);
  }

  uint32 state;
};

template <size_t N>
inline bool any_payload_written(const std::vector<string> &payloads, const std::array<size_t, N> &payload_sizes) {
  for (auto payload_size : payload_sizes) {
    if (std::find(payloads.begin(), payloads.end(), string(payload_size, 'x')) != payloads.end()) {
      return true;
    }
  }
  return false;
}

inline void drain_ipt_until_empty(DecoratorIptFixture &fixture, int write_budget = 64, int max_iters = 40) {
  for (int guard = 0; guard < max_iters && fixture.decorator->get_shaping_wakeup() != 0.0; guard++) {
    auto wakeup = fixture.decorator->get_shaping_wakeup();
    if (wakeup > fixture.clock->now()) {
      fixture.clock->advance(wakeup - fixture.clock->now());
    }
    fixture.inner->writes_per_flush_budget_result = write_budget;
    fixture.decorator->pre_flush_write(fixture.clock->now());
  }
  ASSERT_EQ(0.0, fixture.decorator->get_shaping_wakeup());
}

inline void drain_ipt_until_future_wakeup(DecoratorIptFixture &fixture, int write_budget = 64, int max_iters = 8) {
  for (int guard = 0; guard < max_iters; guard++) {
    auto wakeup = fixture.decorator->get_shaping_wakeup();
    if (wakeup > fixture.clock->now()) {
      break;
    }
    fixture.inner->writes_per_flush_budget_result = write_budget;
    fixture.decorator->pre_flush_write(fixture.clock->now());
  }
}

template <size_t N>
inline void assert_payload_order(const std::vector<string> &payloads, const std::array<size_t, N> &payload_sizes) {
  size_t previous_index = 0;
  for (size_t i = 0; i < N; i++) {
    auto payload = string(payload_sizes[i], 'x');
    auto it = std::find(payloads.begin(), payloads.end(), payload);
    ASSERT_TRUE(it != payloads.end());
    auto index = static_cast<size_t>(it - payloads.begin());
    if (i != 0) {
      ASSERT_TRUE(previous_index < index);
    }
    previous_index = index;
  }
}

template <size_t N>
inline size_t first_future_deadline_index(const std::array<double, N> &deadlines, double now) {
  for (size_t i = 0; i < N; i++) {
    if (deadlines[i] > now) {
      return i;
    }
  }
  return N;
}

template <size_t N>
inline void assert_first_future_deadline_wakeup(const DecoratorIptFixture &fixture,
                                                const std::array<double, N> &deadlines) {
  auto earliest_future = fixture.decorator->get_shaping_wakeup();
  ASSERT_TRUE(earliest_future > fixture.clock->now());
  ASSERT_TRUE(earliest_future >= deadlines.front() - 1e-6);
  ASSERT_TRUE(earliest_future <= deadlines.back() + 1e-6);

  size_t expected_index = first_future_deadline_index(deadlines, fixture.clock->now());
  ASSERT_TRUE(expected_index < N);
  assert_ipt_time_near(deadlines[expected_index], earliest_future);
}

}  // namespace test
}  // namespace mtproto
}  // namespace td