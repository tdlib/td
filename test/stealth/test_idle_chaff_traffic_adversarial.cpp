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
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;

DrsPhaseModel make_exact_record_model(td::int32 target_bytes) {
  return DrsPhaseModel{{RecordSizeBin{target_bytes, target_bytes, 1}}, 1, 0};
}

StealthConfig make_adversarial_config() {
  MockRng rng(9);
  auto config = StealthConfig::default_config(rng);
  config.ipt_params.burst_mu_ms = 0.0;
  config.ipt_params.burst_sigma = 0.0;
  config.ipt_params.burst_max_ms = 1.0;
  config.ipt_params.idle_alpha = 1.0;
  config.ipt_params.idle_scale_ms = 20.0;
  config.ipt_params.idle_max_ms = 40.0;
  config.ipt_params.p_burst_stay = 0.0;
  config.ipt_params.p_idle_to_burst = 0.0;
  config.chaff_policy = ChaffPolicy{};
  config.chaff_policy.enabled = true;
  config.chaff_policy.idle_threshold_ms = 5000;
  config.chaff_policy.min_interval_ms = 10.0;
  config.chaff_policy.max_bytes_per_minute = 1200;
  config.chaff_policy.record_model = make_exact_record_model(300);
  return config;
}

struct Harness final {
  td::unique_ptr<StealthTransportDecorator> transport;
  RecordingTransport *inner{nullptr};
  MockClock *clock{nullptr};

  static Harness create(StealthConfig config) {
    Harness harness;
    auto inner = td::make_unique<RecordingTransport>();
    harness.inner = inner.get();
    auto clock = td::make_unique<MockClock>();
    harness.clock = clock.get();
    auto decorator = StealthTransportDecorator::create(std::move(inner), std::move(config),
                                                       td::make_unique<MockRng>(77), std::move(clock));
    CHECK(decorator.is_ok());
    harness.transport = decorator.move_as_ok();
    return harness;
  }

  void flush_next() {
    auto wakeup = transport->get_shaping_wakeup();
    ASSERT_TRUE(wakeup > clock->now());
    clock->advance(wakeup - clock->now());
    transport->pre_flush_write(clock->now());
  }
};

TEST(IdleChaffTrafficAdversarial, IdleSilencePatternEliminatedByChaffOverDpiWindow) {
  auto harness = Harness::create(make_adversarial_config());

  double previous_time = harness.clock->now();
  for (int i = 0; i < 4; i++) {
    harness.flush_next();
    ASSERT_EQ(i + 1, harness.inner->write_calls);
    auto gap = harness.clock->now() - previous_time;
    ASSERT_TRUE(gap <= 5.040 + 1e-6);
    previous_time = harness.clock->now();
  }
}

TEST(IdleChaffTrafficAdversarial, ChaffBandwidthBudgetRespected) {
  auto harness = Harness::create(make_adversarial_config());
  auto first_window_deadline = harness.clock->now() + 60.0;

  for (int i = 0; i < 16; i++) {
    auto wakeup = harness.transport->get_shaping_wakeup();
    if (wakeup == 0.0 || wakeup > first_window_deadline) {
      break;
    }
    harness.flush_next();
  }

  size_t total_budget_bytes = 0;
  for (auto target_bytes : harness.inner->stealth_record_padding_targets) {
    total_budget_bytes += static_cast<size_t>(target_bytes);
  }

  ASSERT_TRUE(harness.inner->write_calls <= 4);
  ASSERT_TRUE(total_budget_bytes <= 1200u);
  ASSERT_TRUE(harness.transport->get_shaping_wakeup() >= first_window_deadline - 1e-6);
  for (const auto &payload : harness.inner->written_payloads) {
    ASSERT_TRUE(payload.empty());
  }
}

TEST(IdleChaffTrafficAdversarial, ChaffBudgetRecoversAfterWindowExpires) {
  auto harness = Harness::create(make_adversarial_config());
  auto first_window_deadline = harness.clock->now() + 60.0;

  while (true) {
    auto wakeup = harness.transport->get_shaping_wakeup();
    ASSERT_TRUE(wakeup != 0.0);
    if (wakeup > first_window_deadline) {
      break;
    }
    harness.flush_next();
  }

  auto blocked_wakeup = harness.transport->get_shaping_wakeup();
  ASSERT_TRUE(blocked_wakeup > first_window_deadline);
  auto writes_before_resume = harness.inner->write_calls;

  harness.flush_next();

  ASSERT_EQ(writes_before_resume + 1, harness.inner->write_calls);
  ASSERT_TRUE(harness.inner->written_payloads.back().empty());
}

}  // namespace