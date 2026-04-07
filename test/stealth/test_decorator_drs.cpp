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

td::BufferWriter make_test_buffer(td::Slice payload) {
  return td::BufferWriter(payload, 32, 0);
}

DrsPhaseModel make_phase(std::initializer_list<RecordSizeBin> bins, td::int32 max_repeat_run = 1) {
  DrsPhaseModel phase;
  phase.bins.assign(bins.begin(), bins.end());
  phase.max_repeat_run = max_repeat_run;
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
  config.drs_policy.idle_reset_ms_min = 100;
  config.drs_policy.idle_reset_ms_max = 100;
  config.drs_policy.min_payload_cap = 900;
  config.drs_policy.max_payload_cap = 4000;

  auto inner = td::make_unique<RecordingTransport>();
  auto *inner_ptr = inner.get();
  auto clock = td::make_unique<MockClock>();
  auto *clock_ptr = clock.get();
  auto decorator =
      StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(17), std::move(clock));
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr, clock_ptr};
}

TEST(DecoratorDrs, BulkDataUsesSteadyStateCapAcrossFlushesWithoutManualOverride) {
  auto fixture = make_test_decorator();
  ASSERT_FALSE(fixture.inner->max_tls_record_sizes.empty());

  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer("aaaa"), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(4000, fixture.inner->max_tls_record_sizes.back());

  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer(td::Slice(td::string(1200, 'b'))), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(4000, fixture.inner->max_tls_record_sizes.back());

  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer(td::Slice(td::string(1200, 'c'))), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(3, fixture.inner->write_calls);
  ASSERT_EQ(4000, fixture.inner->max_tls_record_sizes.back());
}

TEST(DecoratorDrs, CoalescesCompatibleBurstWithoutReorderingPayloads) {
  auto fixture = make_test_decorator();

  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer("first"), false);
  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer("second"), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(1u, fixture.inner->written_payloads.size());
  ASSERT_EQ("firstsecond", fixture.inner->written_payloads[0]);
}

TEST(DecoratorDrs, QuickAckWriteRemainsPacketScoped) {
  auto fixture = make_test_decorator();

  fixture.decorator->set_traffic_hint(TrafficHint::Interactive);
  fixture.decorator->write(make_test_buffer("first"), true);
  fixture.decorator->set_traffic_hint(TrafficHint::Interactive);
  fixture.decorator->write(make_test_buffer("second"), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());

  auto wakeup = fixture.decorator->get_shaping_wakeup();
  ASSERT_TRUE(wakeup >= fixture.clock->now());
  fixture.decorator->pre_flush_write(wakeup);

  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(2u, fixture.inner->written_payloads.size());
  ASSERT_EQ("first", fixture.inner->written_payloads[0]);
  ASSERT_EQ("second", fixture.inner->written_payloads[1]);
  ASSERT_EQ(2u, fixture.inner->written_quick_acks.size());
  ASSERT_TRUE(fixture.inner->written_quick_acks[0]);
  ASSERT_FALSE(fixture.inner->written_quick_acks[1]);
}

TEST(DecoratorDrs, IdleGapResetsBackToSlowStartCap) {
  auto fixture = make_test_decorator();

  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer(td::Slice(td::string(1200, 'x'))), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());
  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer(td::Slice(td::string(1200, 'y'))), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(4000, fixture.inner->max_tls_record_sizes.back());

  fixture.clock->advance(0.2);
  fixture.decorator->set_traffic_hint(TrafficHint::Interactive);
  fixture.decorator->write(make_test_buffer("z"), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(900, fixture.inner->max_tls_record_sizes.back());
}

}  // namespace