// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Regression guards for ChaffScheduler budget fail-closed behavior.

#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/ChaffScheduler.h"
#include "td/mtproto/stealth/IptController.h"
#include "td/mtproto/stealth/StealthConfig.h"

#include "td/utils/tests.h"

#include <cmath>

namespace {

using td::mtproto::stealth::ChaffScheduler;
using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::IptController;
using td::mtproto::stealth::RecordSizeBin;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::test::MockRng;

StealthConfig make_scheduler_config(size_t max_bytes_per_minute, td::int32 fixed_record_size) {
  MockRng rng(42);
  auto config = StealthConfig::default_config(rng);
  config.chaff_policy.enabled = true;
  config.chaff_policy.idle_threshold_ms = 1;
  config.chaff_policy.min_interval_ms = 1.0;
  config.chaff_policy.max_bytes_per_minute = max_bytes_per_minute;
  config.chaff_policy.record_model = DrsPhaseModel{{RecordSizeBin{fixed_record_size, fixed_record_size, 1}}, 1, 0};
  return config;
}

TEST(SchedulerBudgetRegressionGuard, UnsatisfiableTargetFailsClosedAndDefersWakeupPastNow) {
  MockRng rng(1001);
  auto config = make_scheduler_config(500, 1000);
  IptController ipt(config.ipt_params, rng);
  ChaffScheduler sched(config, ipt, rng, 0.0);

  sched.note_activity(0.0);
  ASSERT_EQ(1000, sched.current_target_bytes());

  const double now = 1.0;
  ASSERT_FALSE(sched.should_emit(now, false, true));

  const double wakeup = sched.get_wakeup(now, false, true);
  ASSERT_TRUE(std::isfinite(wakeup));
  ASSERT_TRUE(wakeup > now);
  ASSERT_TRUE(wakeup >= now + 59.0);
}

TEST(SchedulerBudgetRegressionGuard, BudgetResumeWaitsUntilEnoughBytesAgeOutNotFirstSample) {
  MockRng rng(1002);
  auto config = make_scheduler_config(1000, 600);
  IptController ipt(config.ipt_params, rng);
  ChaffScheduler sched(config, ipt, rng, 0.0);

  sched.note_activity(0.0);
  sched.note_chaff_emitted(1.0, 200);
  sched.note_chaff_emitted(2.0, 700);

  const double now = 3.0;
  ASSERT_FALSE(sched.should_emit(now, false, true));

  const double wakeup = sched.get_wakeup(now, false, true);
  ASSERT_TRUE(std::isfinite(wakeup));
  ASSERT_TRUE(wakeup > 61.5);
}

TEST(SchedulerBudgetRegressionGuard, OversizedTargetNeverTurnsSendableAfterBudgetSamplesExpire) {
  MockRng rng(1003);
  auto config = make_scheduler_config(500, 1000);
  IptController ipt(config.ipt_params, rng);
  ChaffScheduler sched(config, ipt, rng, 0.0);

  sched.note_activity(0.0);
  sched.note_chaff_emitted(1.0, 400);
  sched.note_chaff_emitted(10.0, 400);

  const double now = 200.0;
  ASSERT_FALSE(sched.should_emit(now, false, true));

  const double wakeup = sched.get_wakeup(now, false, true);
  ASSERT_TRUE(std::isfinite(wakeup));
  ASSERT_TRUE(wakeup > now);
}

}  // namespace
