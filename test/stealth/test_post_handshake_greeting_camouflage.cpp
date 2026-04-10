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
using td::mtproto::stealth::GreetingCamouflagePolicy;
using td::mtproto::stealth::RecordSizeBin;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;

DrsPhaseModel make_exact_phase(td::int32 record_size) {
  DrsPhaseModel phase;
  phase.bins = {{record_size, record_size, 1}};
  phase.max_repeat_run = 1;
  phase.local_jitter = 0;
  return phase;
}

td::BufferWriter make_test_buffer(td::Slice payload) {
  return td::BufferWriter(payload, 32, 0);
}

StealthConfig make_greeting_config() {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_exact_phase(1400);
  config.drs_policy.congestion_open = make_exact_phase(1800);
  config.drs_policy.steady_state = make_exact_phase(2400);
  config.drs_policy.slow_start_records = 16;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.idle_reset_ms_min = 1000;
  config.drs_policy.idle_reset_ms_max = 1000;
  config.drs_policy.min_payload_cap = 256;
  config.drs_policy.max_payload_cap = 2400;

  GreetingCamouflagePolicy greeting_policy;
  greeting_policy.greeting_record_count = 3;
  greeting_policy.record_models[0] = make_exact_phase(320);
  greeting_policy.record_models[1] = make_exact_phase(640);
  greeting_policy.record_models[2] = make_exact_phase(960);
  greeting_policy.record_models[3] = make_exact_phase(1200);
  greeting_policy.record_models[4] = make_exact_phase(1440);
  config.greeting_camouflage_policy = greeting_policy;
  return config;
}

struct DecoratorFixture final {
  td::unique_ptr<StealthTransportDecorator> decorator;
  RecordingTransport *inner{nullptr};
  MockClock *clock{nullptr};
};

DecoratorFixture make_fixture(StealthConfig config) {
  auto inner = td::make_unique<RecordingTransport>();
  inner->supports_tls_record_sizing_result = true;
  auto *inner_ptr = inner.get();

  auto clock = td::make_unique<MockClock>();
  auto *clock_ptr = clock.get();

  auto decorator = StealthTransportDecorator::create(std::move(inner), std::move(config), td::make_unique<MockRng>(7),
                                                     std::move(clock));
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr, clock_ptr};
}

void flush_once(DecoratorFixture &fixture, td::Slice payload = "x", TrafficHint hint = TrafficHint::Interactive,
                bool quick_ack = false) {
  fixture.decorator->set_traffic_hint(hint);
  fixture.decorator->write(make_test_buffer(payload), quick_ack);
  fixture.decorator->pre_flush_write(fixture.clock->now());
  auto wakeup = fixture.decorator->get_shaping_wakeup();
  if (wakeup > fixture.clock->now()) {
    fixture.clock->advance(wakeup - fixture.clock->now());
    fixture.decorator->pre_flush_write(fixture.clock->now());
  }
}

TEST(PostHandshakeGreetingCamouflage, FirstRecordMatchesGreetingTemplate) {
  auto fixture = make_fixture(make_greeting_config());

  flush_once(fixture, "first");

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_FALSE(fixture.inner->stealth_record_padding_targets.empty());
  ASSERT_EQ(320, fixture.inner->stealth_record_padding_targets.back());
}

TEST(PostHandshakeGreetingCamouflage, SecondRecordMatchesGreetingTemplate) {
  auto fixture = make_fixture(make_greeting_config());

  flush_once(fixture, "first");
  flush_once(fixture, "second");

  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_TRUE(fixture.inner->stealth_record_padding_targets.size() >= 2u);
  ASSERT_EQ(640, fixture.inner->stealth_record_padding_targets.back());
}

TEST(PostHandshakeGreetingCamouflage, ThirdRecordMatchesGreetingTemplate) {
  auto fixture = make_fixture(make_greeting_config());

  flush_once(fixture, "first");
  flush_once(fixture, "second");
  flush_once(fixture, "third");

  ASSERT_EQ(3, fixture.inner->write_calls);
  ASSERT_TRUE(fixture.inner->stealth_record_padding_targets.size() >= 3u);
  ASSERT_EQ(960, fixture.inner->stealth_record_padding_targets.back());
}

TEST(PostHandshakeGreetingCamouflage, FourthRecordUsesDrsNotGreeting) {
  auto fixture = make_fixture(make_greeting_config());

  flush_once(fixture, "first");
  flush_once(fixture, "second");
  flush_once(fixture, "third");
  flush_once(fixture, "fourth");

  ASSERT_EQ(4, fixture.inner->write_calls);
  ASSERT_TRUE(fixture.inner->stealth_record_padding_targets.size() >= 4u);
  ASSERT_EQ(1400, fixture.inner->stealth_record_padding_targets.back());
}

TEST(PostHandshakeGreetingCamouflage, GreetingDisabledProducesNormalDrsSizes) {
  auto config = make_greeting_config();
  config.greeting_camouflage_policy.greeting_record_count = 0;

  auto fixture = make_fixture(std::move(config));
  flush_once(fixture, "first");

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_FALSE(fixture.inner->stealth_record_padding_targets.empty());
  ASSERT_EQ(1400, fixture.inner->stealth_record_padding_targets.back());
}

TEST(PostHandshakeGreetingCamouflage, GreetingSizesStayWithinBrowserFirstFlightRange) {
  auto config = make_greeting_config();
  ASSERT_TRUE(config.validate().is_ok());

  for (td::uint8 i = 0; i < config.greeting_camouflage_policy.greeting_record_count; i++) {
    const auto &model = config.greeting_camouflage_policy.record_models[i];
    for (const auto &bin : model.bins) {
      ASSERT_TRUE(bin.lo >= 80);
      ASSERT_TRUE(bin.hi <= 1500);
      ASSERT_TRUE(bin.lo <= bin.hi);
    }
  }
}

TEST(PostHandshakeGreetingCamouflage, GreetingRecordCountRejectsOutOfRangeCount) {
  auto config = make_greeting_config();
  config.greeting_camouflage_policy.greeting_record_count = 6;

  auto status = config.validate();
  ASSERT_TRUE(status.is_error());
}

TEST(PostHandshakeGreetingCamouflage, GreetingDoesNotAffectIncomingRecords) {
  auto fixture = make_fixture(make_greeting_config());
  td::BufferSlice message;
  td::uint32 quick_ack = 0;

  auto result = fixture.decorator->read_next(&message, &quick_ack);

  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(fixture.inner->stealth_record_padding_targets.empty());
  ASSERT_EQ(0, fixture.inner->write_calls);
  ASSERT_EQ(fixture.inner->next_read_message.size(), result.ok());
}

TEST(PostHandshakeGreetingCamouflage, EmptyFlushDoesNotConsumeGreetingSlot) {
  auto fixture = make_fixture(make_greeting_config());

  fixture.decorator->pre_flush_write(fixture.clock->now());
  flush_once(fixture, "first");

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_FALSE(fixture.inner->stealth_record_padding_targets.empty());
  ASSERT_EQ(320, fixture.inner->stealth_record_padding_targets.back());
}

TEST(PostHandshakeGreetingCamouflage, GreetingPhaseDoesNotConsumeDrsSlowStartBudget) {
  auto config = make_greeting_config();
  config.drs_policy.slow_start_records = 1;

  auto fixture = make_fixture(std::move(config));
  flush_once(fixture, "first");
  flush_once(fixture, "second");
  flush_once(fixture, "third");
  flush_once(fixture, "fourth");
  ASSERT_EQ(1400, fixture.inner->stealth_record_padding_targets.back());

  flush_once(fixture, "fifth");
  ASSERT_EQ(1800, fixture.inner->stealth_record_padding_targets.back());
}

}  // namespace