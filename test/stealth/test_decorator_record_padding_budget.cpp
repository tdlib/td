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

td::BufferWriter make_test_buffer(size_t size) {
  return td::BufferWriter(td::Slice(td::string(size, 'x')), 32, 0);
}

DrsPhaseModel make_phase(td::int32 cap) {
  DrsPhaseModel phase;
  phase.bins = {RecordSizeBin{cap, cap, 1}};
  phase.max_repeat_run = 1;
  phase.local_jitter = 0;
  return phase;
}

struct DecoratorFixture final {
  td::unique_ptr<StealthTransportDecorator> decorator;
  RecordingTransport *inner{nullptr};
  MockClock *clock{nullptr};
};

void flush_ready(DecoratorFixture &fixture) {
  fixture.decorator->pre_flush_write(fixture.clock->now());
  auto wakeup = fixture.decorator->get_shaping_wakeup();
  if (wakeup > fixture.clock->now()) {
    fixture.decorator->pre_flush_write(wakeup);
  }
}

DecoratorFixture make_budget_fixture() {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_phase(256);
  config.drs_policy.congestion_open = make_phase(256);
  config.drs_policy.steady_state = make_phase(256);
  config.drs_policy.slow_start_records = 1024;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.min_payload_cap = 256;
  config.drs_policy.max_payload_cap = 256;
  config.record_padding_policy.small_record_threshold = 400;
  config.record_padding_policy.small_record_max_fraction = 0.2;
  config.record_padding_policy.small_record_window_size = 5;
  config.record_padding_policy.target_tolerance = 32;

  auto inner = td::make_unique<RecordingTransport>();
  auto *inner_ptr = inner.get();
  auto clock = td::make_unique<MockClock>();
  auto *clock_ptr = clock.get();
  auto decorator =
      StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(11), std::move(clock));
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr, clock_ptr};
}

TEST(DecoratorRecordPaddingBudget, ExhaustedBudgetClampsSubThresholdTargetsToThreshold) {
  auto fixture = make_budget_fixture();

  for (int i = 0; i < 4; i++) {
    fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
    fixture.decorator->write(make_test_buffer(8 + i), false);
    flush_ready(fixture);
  }

  ASSERT_EQ(4, fixture.inner->write_calls);
  ASSERT_EQ(8u, fixture.inner->stealth_record_padding_targets.size());
  ASSERT_EQ(256, fixture.inner->stealth_record_padding_targets[1]);
  ASSERT_EQ(400, fixture.inner->stealth_record_padding_targets[3]);
  ASSERT_EQ(400, fixture.inner->stealth_record_padding_targets[5]);
  ASSERT_EQ(400, fixture.inner->stealth_record_padding_targets[7]);
}

TEST(DecoratorRecordPaddingBudget, RollingWindowAllowsSmallTargetsAgainAfterLargerTargetsAgeOutBudget) {
  auto fixture = make_budget_fixture();

  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer(8), false);
  flush_ready(fixture);

  for (int i = 0; i < 5; i++) {
    fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
    fixture.decorator->write(make_test_buffer(16 + i), false);
    flush_ready(fixture);
  }

  ASSERT_FALSE(fixture.inner->stealth_record_padding_targets.empty());

  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer(32), false);
  flush_ready(fixture);

  ASSERT_EQ(256, fixture.inner->stealth_record_padding_targets.back());
}

}  // namespace