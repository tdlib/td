// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockClock.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/RecordingTransport.h"

#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"

#include "td/utils/buffer.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::RecordSizeBin;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;

td::BufferWriter make_test_buffer(size_t size, char fill) {
  return td::BufferWriter(td::Slice(td::string(size, fill)), 32, 0);
}

DrsPhaseModel make_phase(std::initializer_list<RecordSizeBin> bins) {
  DrsPhaseModel phase;
  phase.bins.assign(bins.begin(), bins.end());
  phase.max_repeat_run = 1;
  phase.local_jitter = 0;
  return phase;
}

struct DecoratorFixture final {
  td::unique_ptr<StealthTransportDecorator> decorator;
  RecordingTransport *inner{nullptr};
  MockClock *clock{nullptr};
};

DecoratorFixture make_test_decorator() {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_phase({{900, 900, 1}});
  config.drs_policy.congestion_open = make_phase({{1400, 1400, 1}});
  config.drs_policy.steady_state = make_phase({{4000, 4000, 1}});
  config.drs_policy.slow_start_records = 1;
  config.drs_policy.congestion_bytes = 1000;
  config.drs_policy.idle_reset_ms_min = 1000;
  config.drs_policy.idle_reset_ms_max = 1000;
  config.drs_policy.min_payload_cap = 900;
  config.drs_policy.max_payload_cap = 4000;

  auto inner = td::make_unique<RecordingTransport>();
  inner->writes_per_flush_budget_result = 1;
  auto *inner_ptr = inner.get();
  auto clock = td::make_unique<MockClock>();
  auto *clock_ptr = clock.get();
  auto decorator =
      StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(17), std::move(clock));
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr, clock_ptr};
}

TEST(DecoratorDrsPartialBudget, CompatibleBurstCoalescesIntoSingleBudgetedWrite) {
  auto fixture = make_test_decorator();

  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer(700, 'a'), false);
  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer(700, 'b'), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(1u, fixture.inner->written_payloads.size());
  ASSERT_EQ(td::string(700, 'a') + td::string(700, 'b'), fixture.inner->written_payloads[0]);
  ASSERT_EQ(0.0, fixture.decorator->get_shaping_wakeup());
}

TEST(DecoratorDrsPartialBudget, IncompatibleBurstLeavesTailQueuedForNextBudgetCycle) {
  auto fixture = make_test_decorator();

  fixture.decorator->set_traffic_hint(TrafficHint::Keepalive);
  fixture.decorator->write(make_test_buffer(17, 'k'), true);
  fixture.decorator->set_traffic_hint(TrafficHint::Keepalive);
  fixture.decorator->write(make_test_buffer(19, 'q'), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(1u, fixture.inner->written_payloads.size());
  ASSERT_EQ(td::string(17, 'k'), fixture.inner->written_payloads[0]);
  ASSERT_TRUE(fixture.decorator->get_shaping_wakeup() >= fixture.clock->now());

  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(2u, fixture.inner->written_payloads.size());
  ASSERT_EQ(td::string(19, 'q'), fixture.inner->written_payloads[1]);
  ASSERT_EQ(0.0, fixture.decorator->get_shaping_wakeup());
}

}  // namespace
