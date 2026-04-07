//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
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
  MockRng rng(61);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_phase({{900, 900, 1}});
  config.drs_policy.congestion_open = make_phase({{1400, 1400, 1}});
  config.drs_policy.steady_state = make_phase({{2400, 2400, 1}});
  config.drs_policy.slow_start_records = 2;
  config.drs_policy.congestion_bytes = 2800;
  config.drs_policy.idle_reset_ms_min = 1000;
  config.drs_policy.idle_reset_ms_max = 1000;
  config.drs_policy.min_payload_cap = 900;
  config.drs_policy.max_payload_cap = 2400;

  auto inner = td::make_unique<RecordingTransport>();
  auto *inner_ptr = inner.get();
  auto clock = td::make_unique<MockClock>();
  auto *clock_ptr = clock.get();
  auto decorator =
      StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(62), std::move(clock));
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr, clock_ptr};
}

td::BufferWriter make_test_buffer(td::Slice payload) {
  return td::BufferWriter(payload, 32, 0);
}

TEST(DecoratorDrsZeroWriteAdversarial, EmptyFlushDoesNotAdvanceInteractivePhase) {
  auto fixture = make_test_decorator();
  ASSERT_FALSE(fixture.inner->max_tls_record_sizes.empty());

  fixture.decorator->set_traffic_hint(TrafficHint::Interactive);
  fixture.decorator->write(make_test_buffer(td::Slice()), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(900, fixture.inner->max_tls_record_sizes.back());

  fixture.decorator->set_traffic_hint(TrafficHint::Interactive);
  fixture.decorator->write(make_test_buffer("real"), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(900, fixture.inner->max_tls_record_sizes.back());

  fixture.decorator->set_traffic_hint(TrafficHint::Interactive);
  fixture.decorator->write(make_test_buffer("next"), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(3, fixture.inner->write_calls);
  ASSERT_EQ(900, fixture.inner->max_tls_record_sizes.back());

  fixture.decorator->set_traffic_hint(TrafficHint::Interactive);
  fixture.decorator->write(make_test_buffer("phase-open"), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(4, fixture.inner->write_calls);
  ASSERT_EQ(1400, fixture.inner->max_tls_record_sizes.back());
}

}  // namespace