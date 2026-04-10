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

#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::ChaffPolicy;
using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::RecordSizeBin;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;

DrsPhaseModel make_exact_record_model(td::int32 target_bytes) {
  return DrsPhaseModel{{RecordSizeBin{target_bytes, target_bytes, 1}}, 1, 0};
}

StealthConfig make_idle_chaff_config() {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_exact_record_model(900);
  config.drs_policy.congestion_open = make_exact_record_model(900);
  config.drs_policy.steady_state = make_exact_record_model(900);
  config.drs_policy.slow_start_records = 64;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.min_payload_cap = 256;
  config.drs_policy.max_payload_cap = 900;
  config.ipt_params.burst_mu_ms = 0.0;
  config.ipt_params.burst_sigma = 0.0;
  config.ipt_params.burst_max_ms = 1.0;
  config.ipt_params.idle_alpha = 1.0;
  config.ipt_params.idle_scale_ms = 25.0;
  config.ipt_params.idle_max_ms = 50.0;
  config.ipt_params.p_burst_stay = 0.0;
  config.ipt_params.p_idle_to_burst = 0.0;

  config.chaff_policy = ChaffPolicy{};
  config.chaff_policy.enabled = true;
  config.chaff_policy.idle_threshold_ms = 5000;
  config.chaff_policy.min_interval_ms = 10.0;
  config.chaff_policy.max_bytes_per_minute = 8192;
  config.chaff_policy.record_model = make_exact_record_model(384);
  return config;
}

struct Harness final {
  td::unique_ptr<StealthTransportDecorator> transport;
  RecordingTransport *inner{nullptr};
  MockClock *clock{nullptr};

  static Harness create(StealthConfig config, td::uint64 seed = 41) {
    Harness harness;
    auto inner = td::make_unique<RecordingTransport>();
    harness.inner = inner.get();
    auto clock = td::make_unique<MockClock>();
    harness.clock = clock.get();
    auto decorator = StealthTransportDecorator::create(std::move(inner), std::move(config),
                                                       td::make_unique<MockRng>(seed), std::move(clock));
    CHECK(decorator.is_ok());
    harness.transport = decorator.move_as_ok();
    return harness;
  }

  void queue_real_write(td::Slice payload, TrafficHint hint = TrafficHint::Interactive) {
    transport->set_traffic_hint(hint);
    td::BufferWriter writer(payload, transport->max_prepend_size(), transport->max_append_size());
    transport->write(std::move(writer), false);
  }

  void flush_at(double when) {
    if (when > clock->now()) {
      clock->advance(when - clock->now());
    }
    transport->pre_flush_write(clock->now());
  }
};

TEST(IdleChaffTraffic, ChaffInjectedAfterIdleThreshold) {
  auto harness = Harness::create(make_idle_chaff_config());

  auto wakeup = harness.transport->get_shaping_wakeup();
  ASSERT_TRUE(wakeup > harness.clock->now());
  ASSERT_TRUE(wakeup >= harness.clock->now() + 5.0 - 1e-6);

  harness.flush_at(wakeup);

  ASSERT_EQ(1, harness.inner->write_calls);
  ASSERT_EQ(1u, harness.inner->written_payloads.size());
  ASSERT_TRUE(harness.inner->written_payloads[0].empty());
  ASSERT_FALSE(harness.inner->stealth_record_padding_targets.empty());
  ASSERT_EQ(384, harness.inner->stealth_record_padding_targets.back());
}

TEST(IdleChaffTraffic, ChaffStopsDuringActiveTraffic) {
  auto harness = Harness::create(make_idle_chaff_config());

  auto initial_wakeup = harness.transport->get_shaping_wakeup();
  ASSERT_TRUE(initial_wakeup > harness.clock->now());

  harness.clock->advance(1.0);
  harness.queue_real_write(td::Slice("user"));
  ASSERT_TRUE(harness.transport->get_shaping_wakeup() <= harness.clock->now() + 1e-6);

  harness.flush_at(harness.clock->now());

  ASSERT_EQ(1, harness.inner->write_calls);
  ASSERT_EQ(td::string("user"), harness.inner->written_payloads[0]);

  auto next_wakeup = harness.transport->get_shaping_wakeup();
  ASSERT_TRUE(next_wakeup >= harness.clock->now() + 5.0 - 1e-6);
  ASSERT_EQ(0u,
            std::count(harness.inner->written_payloads.begin(), harness.inner->written_payloads.end(), td::string()));
}

TEST(IdleChaffTraffic, ChaffDisabledByPolicyProducesNoTraffic) {
  auto config = make_idle_chaff_config();
  config.chaff_policy.enabled = false;
  auto harness = Harness::create(std::move(config));

  ASSERT_EQ(0.0, harness.transport->get_shaping_wakeup());
  harness.flush_at(harness.clock->now() + 60.0);

  ASSERT_EQ(0, harness.inner->write_calls);
  ASSERT_TRUE(harness.inner->written_payloads.empty());
}

TEST(IdleChaffTraffic, ChaffRecordSizeMatchesPolicy) {
  auto harness = Harness::create(make_idle_chaff_config());

  auto wakeup = harness.transport->get_shaping_wakeup();
  harness.flush_at(wakeup);

  ASSERT_FALSE(harness.inner->stealth_record_padding_targets.empty());
  ASSERT_EQ(384, harness.inner->stealth_record_padding_targets.back());
  ASSERT_FALSE(harness.inner->max_tls_record_sizes.empty());
  ASSERT_TRUE(harness.inner->max_tls_record_sizes.back() >= 384);
}

TEST(IdleChaffTraffic, ChaffNotInjectedWhenInnerTransportCannotWrite) {
  auto harness = Harness::create(make_idle_chaff_config());
  harness.inner->can_write_result = false;

  ASSERT_EQ(0.0, harness.transport->get_shaping_wakeup());
  harness.flush_at(harness.clock->now() + 30.0);

  ASSERT_EQ(0, harness.inner->write_calls);
  ASSERT_TRUE(harness.inner->written_payloads.empty());
}

}  // namespace