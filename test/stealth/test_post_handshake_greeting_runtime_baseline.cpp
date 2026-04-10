// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockClock.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/RecordingTransport.h"

#include "td/mtproto/ProxySecret.h"
#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"

#include "td/utils/buffer.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::ProxySecret;
using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;

td::string make_tls_secret() {
  return td::string("\xee") + "0123456789abcdefexample.com";
}

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

bool value_is_in_model(td::int32 value, const DrsPhaseModel &model) {
  for (const auto &bin : model.bins) {
    if (bin.lo <= value && value <= bin.hi) {
      return true;
    }
  }
  return false;
}

struct DecoratorFixture final {
  td::unique_ptr<StealthTransportDecorator> decorator;
  RecordingTransport *inner{nullptr};
  MockClock *clock{nullptr};
  StealthConfig config;
};

DecoratorFixture make_fixture(td::uint64 seed) {
  MockRng config_rng(seed);
  auto config = StealthConfig::from_secret(ProxySecret::from_raw(make_tls_secret()), config_rng);
  config.drs_policy.slow_start = make_exact_phase(1400);
  config.drs_policy.congestion_open = make_exact_phase(1400);
  config.drs_policy.steady_state = make_exact_phase(1400);
  config.drs_policy.slow_start_records = 32;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.idle_reset_ms_min = 1000;
  config.drs_policy.idle_reset_ms_max = 1000;
  config.drs_policy.min_payload_cap = 256;
  config.drs_policy.max_payload_cap = 1400;
  CHECK(config.validate().is_ok());
  auto config_snapshot = config;

  auto inner = td::make_unique<RecordingTransport>();
  inner->supports_tls_record_sizing_result = true;
  auto *inner_ptr = inner.get();

  auto clock = td::make_unique<MockClock>();
  auto *clock_ptr = clock.get();

  auto decorator = StealthTransportDecorator::create(std::move(inner), std::move(config),
                                                     td::make_unique<MockRng>(seed), std::move(clock));
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr, clock_ptr, std::move(config_snapshot)};
}

void flush_until_next_write(DecoratorFixture &fixture) {
  auto writes_before = fixture.inner->write_calls;
  while (fixture.inner->write_calls == writes_before) {
    fixture.decorator->pre_flush_write(fixture.clock->now());
    if (fixture.inner->write_calls != writes_before) {
      break;
    }
    auto wakeup = fixture.decorator->get_shaping_wakeup();
    ASSERT_TRUE(wakeup > fixture.clock->now());
    fixture.clock->advance(wakeup - fixture.clock->now());
  }
}

void flush_once(DecoratorFixture &fixture, td::Slice payload = "x", TrafficHint hint = TrafficHint::Interactive) {
  fixture.decorator->set_traffic_hint(hint);
  fixture.decorator->write(make_test_buffer(payload), false);
  flush_until_next_write(fixture);
}

TEST(PostHandshakeGreetingRuntimeBaseline, TlsSecretGreetingUsesFiveBaselineRecordsBeforeDrs) {
  auto fixture = make_fixture(31);

  for (td::uint8 record_index = 0; record_index < 5; record_index++) {
    flush_once(fixture, "msg");
    ASSERT_TRUE(fixture.inner->stealth_record_padding_targets.size() >= static_cast<size_t>(record_index + 1));
    auto target = fixture.inner->stealth_record_padding_targets.back();
    ASSERT_TRUE(value_is_in_model(target, fixture.config.greeting_camouflage_policy.record_models[record_index]));
  }

  flush_once(fixture, "post-greeting");
  ASSERT_EQ(6, fixture.inner->write_calls);
  ASSERT_EQ(1400, fixture.inner->stealth_record_padding_targets.back());
}

TEST(PostHandshakeGreetingRuntimeBaseline, GreetingCoalescedFlushConsumesSingleBaselineSlot) {
  auto fixture = make_fixture(47);

  fixture.decorator->set_traffic_hint(TrafficHint::AuthHandshake);
  fixture.decorator->write(make_test_buffer("first"), false);
  fixture.decorator->set_traffic_hint(TrafficHint::AuthHandshake);
  fixture.decorator->write(make_test_buffer("second"), false);
  flush_until_next_write(fixture);

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_FALSE(fixture.inner->stealth_record_padding_targets.empty());
  ASSERT_TRUE(value_is_in_model(fixture.inner->stealth_record_padding_targets.back(),
                                fixture.config.greeting_camouflage_policy.record_models[0]));

  flush_once(fixture, "third", TrafficHint::AuthHandshake);

  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_TRUE(fixture.inner->stealth_record_padding_targets.size() >= 2u);
  ASSERT_TRUE(value_is_in_model(fixture.inner->stealth_record_padding_targets.back(),
                                fixture.config.greeting_camouflage_policy.record_models[1]));
}

}  // namespace