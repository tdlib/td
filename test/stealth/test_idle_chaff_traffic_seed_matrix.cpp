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

#include <set>

namespace {

using td::mtproto::stealth::ChaffPolicy;
using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::RecordSizeBin;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;

DrsPhaseModel make_seed_matrix_record_model() {
  return DrsPhaseModel{{RecordSizeBin{240, 260, 1}, RecordSizeBin{320, 340, 1}, RecordSizeBin{480, 520, 1}}, 2, 0};
}

DrsPhaseModel make_exact_record_model(td::int32 target_bytes) {
  return DrsPhaseModel{{RecordSizeBin{target_bytes, target_bytes, 1}}, 1, 0};
}

StealthConfig make_seed_matrix_config() {
  MockRng rng(5);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_exact_record_model(520);
  config.drs_policy.congestion_open = make_exact_record_model(520);
  config.drs_policy.steady_state = make_exact_record_model(520);
  config.drs_policy.slow_start_records = 64;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.min_payload_cap = 256;
  config.drs_policy.max_payload_cap = 520;
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
  config.chaff_policy.max_bytes_per_minute = 6000;
  config.chaff_policy.record_model = make_seed_matrix_record_model();
  return config;
}

bool is_allowed_target(td::int32 target) {
  return (240 <= target && target <= 260) || (320 <= target && target <= 340) || (480 <= target && target <= 520);
}

struct Harness final {
  td::unique_ptr<StealthTransportDecorator> transport;
  RecordingTransport *inner{nullptr};
  MockClock *clock{nullptr};

  static Harness create(td::uint64 seed) {
    Harness harness;
    auto inner = td::make_unique<RecordingTransport>();
    harness.inner = inner.get();
    auto clock = td::make_unique<MockClock>();
    harness.clock = clock.get();
    auto decorator = StealthTransportDecorator::create(std::move(inner), make_seed_matrix_config(),
                                                       td::make_unique<MockRng>(seed), std::move(clock));
    CHECK(decorator.is_ok());
    harness.transport = decorator.move_as_ok();
    return harness;
  }

  double flush_next() {
    auto wakeup = transport->get_shaping_wakeup();
    ASSERT_TRUE(wakeup > clock->now());
    clock->advance(wakeup - clock->now());
    transport->pre_flush_write(clock->now());
    return clock->now();
  }
};

TEST(IdleChaffTrafficSeedMatrix, TargetsStayWithinConfiguredBinsAcrossDeterministicSeeds) {
  std::set<td::int32> observed_targets;

  for (td::uint64 seed = 1; seed <= 16; seed++) {
    auto harness = Harness::create(seed);
    for (int cycle = 0; cycle < 4; cycle++) {
      harness.flush_next();
      ASSERT_FALSE(harness.inner->stealth_record_padding_targets.empty());
      const auto target = harness.inner->stealth_record_padding_targets.back();
      ASSERT_TRUE(is_allowed_target(target));
      observed_targets.insert(target);
    }
  }

  ASSERT_TRUE(observed_targets.size() >= 3u);
}

TEST(IdleChaffTrafficSeedMatrix, EmissionTimesStayStrictlyIncreasingAcrossDeterministicSeeds) {
  for (td::uint64 seed = 1; seed <= 16; seed++) {
    auto harness = Harness::create(seed);
    double previous_emit_at = 0.0;
    for (int cycle = 0; cycle < 4; cycle++) {
      const auto emitted_at = harness.flush_next();
      ASSERT_TRUE(emitted_at > previous_emit_at);
      if (previous_emit_at != 0.0) {
        ASSERT_TRUE(emitted_at - previous_emit_at >= 0.010 - 1e-6);
      }
      previous_emit_at = emitted_at;
    }
  }
}

}  // namespace