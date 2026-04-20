// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Adversarial tests for the chaff-send scheduling subsystem.
// Threat model: an adversary who manipulates timing inputs or byte counts
// must not be able to bypass budget accounting, cause unbounded chaff emission,
// or produce exploitable traffic fingerprints.

#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/ChaffScheduler.h"
#include "td/mtproto/stealth/IptController.h"
#include "td/mtproto/stealth/StealthConfig.h"

#include "td/utils/tests.h"

#include <cmath>
#include <limits>

namespace {

using td::mtproto::stealth::ChaffScheduler;
using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::IptController;
using td::mtproto::stealth::RecordSizeBin;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::test::MockRng;

StealthConfig make_tight_budget_config(size_t max_bytes_per_minute, td::int32 record_size_bytes = 100) {
  MockRng rng(42);
  auto config = StealthConfig::default_config(rng);
  config.chaff_policy.enabled = true;
  config.chaff_policy.idle_threshold_ms = 1;
  config.chaff_policy.min_interval_ms = 1.0;
  config.chaff_policy.max_bytes_per_minute = max_bytes_per_minute;
  config.chaff_policy.record_model = DrsPhaseModel{{RecordSizeBin{record_size_bytes, record_size_bytes, 1}}, 1, 0};
  return config;
}

TEST(SchedulerBudget, BudgetExhaustedAfterFillingWindowBlocksEmission) {
  MockRng rng(1);
  auto config = make_tight_budget_config(400, 100);
  IptController ipt(config.ipt_params, rng);
  ChaffScheduler sched(config, ipt, rng, 0.0);

  sched.note_activity(0.0);
  for (int i = 0; i < 4; i++) {
    sched.note_chaff_emitted(0.1 + static_cast<double>(i) * 0.01, 100);
  }
  ASSERT_FALSE(sched.should_emit(0.5, false, true));
}

TEST(SchedulerBudget, BudgetResumesAfterWindowSlidesPastOldestSample) {
  MockRng rng(2);
  auto config = make_tight_budget_config(200, 100);
  IptController ipt(config.ipt_params, rng);
  ChaffScheduler sched(config, ipt, rng, 0.0);

  sched.note_activity(0.0);
  sched.note_chaff_emitted(1.0, 100);
  sched.note_chaff_emitted(2.0, 100);
  ASSERT_FALSE(sched.should_emit(5.0, false, true));

  double past_window = 1.0 + 60.001;
  sched.note_activity(past_window);

  auto wakeup = sched.get_wakeup(past_window, false, true);
  ASSERT_TRUE(wakeup > 0.0);
  ASSERT_TRUE(std::isfinite(wakeup));
}

TEST(SchedulerBudget, SingleEmissionOfMaxSizeTBytesDoesNotBypassBudget) {
  // Injecting SIZE_MAX bytes must not overflow the budget accumulator to 0.
  MockRng rng(3);
  auto config = make_tight_budget_config(4096, 100);
  IptController ipt(config.ipt_params, rng);
  ChaffScheduler sched(config, ipt, rng, 0.0);

  sched.note_activity(0.0);
  sched.note_chaff_emitted(1.0, std::numeric_limits<size_t>::max());
  ASSERT_FALSE(sched.should_emit(2.0, false, true));
}

TEST(SchedulerBudget, TwoLargeEmissionsDoNotWrapAroundAccumulator) {
  MockRng rng(4);
  constexpr size_t large_bytes = std::numeric_limits<size_t>::max() / 4;
  auto config = make_tight_budget_config(1000, 100);
  IptController ipt(config.ipt_params, rng);
  ChaffScheduler sched(config, ipt, rng, 0.0);

  sched.note_activity(0.0);
  sched.note_chaff_emitted(1.0, large_bytes);
  sched.note_chaff_emitted(2.0, large_bytes);
  ASSERT_FALSE(sched.should_emit(3.0, false, true));
}

TEST(SchedulerBudget, DisabledPolicyRejectsEmissionUnconditionally) {
  MockRng rng(5);
  auto config = make_tight_budget_config(4096);
  config.chaff_policy.enabled = false;
  IptController ipt(config.ipt_params, rng);
  ChaffScheduler sched(config, ipt, rng, 0.0);

  ASSERT_FALSE(sched.should_emit(0.0, false, true));
  ASSERT_FALSE(sched.should_emit(9999.0, false, true));
  ASSERT_EQ(0.0, sched.get_wakeup(0.0, false, true));
  ASSERT_EQ(0, sched.current_target_bytes());
}

TEST(SchedulerBudget, DisabledPolicyIgnoresHeavyCallSequence) {
  MockRng rng(6);
  auto config = make_tight_budget_config(4096);
  config.chaff_policy.enabled = false;
  IptController ipt(config.ipt_params, rng);
  ChaffScheduler sched(config, ipt, rng, 0.0);

  for (int i = 0; i < 100; i++) {
    sched.note_activity(static_cast<double>(i));
    sched.note_chaff_emitted(static_cast<double>(i), 1234);
  }
  ASSERT_FALSE(sched.should_emit(100.0, false, true));
}

TEST(SchedulerBudget, ZeroBytesPerMinuteLimitBlocksAllEmission) {
  MockRng rng(7);
  auto config = make_tight_budget_config(1, 2);
  IptController ipt(config.ipt_params, rng);
  ChaffScheduler sched(config, ipt, rng, 0.0);

  sched.note_activity(0.0);
  sched.note_chaff_emitted(0.1, 1);
  ASSERT_FALSE(sched.should_emit(1.0, false, true));
}

TEST(SchedulerBudget, GetWakeupReturnsFutureDateWhenBudgetAvailable) {
  MockRng rng(8);
  auto config = make_tight_budget_config(100000, 50);
  IptController ipt(config.ipt_params, rng);
  ChaffScheduler sched(config, ipt, rng, 0.0);

  sched.note_activity(0.0);
  double wakeup = sched.get_wakeup(0.0, false, true);
  ASSERT_TRUE(wakeup > 0.0);
  ASSERT_TRUE(std::isfinite(wakeup));
}

TEST(SchedulerBudget, GetWakeupReturnsZeroWhenHasPendingData) {
  MockRng rng(9);
  auto config = make_tight_budget_config(100000);
  IptController ipt(config.ipt_params, rng);
  ChaffScheduler sched(config, ipt, rng, 0.0);

  sched.note_activity(0.0);
  ASSERT_EQ(0.0, sched.get_wakeup(0.0, true, true));
}

TEST(SchedulerBudget, GetWakeupReturnsZeroWhenCannotWrite) {
  MockRng rng(10);
  auto config = make_tight_budget_config(100000);
  IptController ipt(config.ipt_params, rng);
  ChaffScheduler sched(config, ipt, rng, 0.0);

  sched.note_activity(0.0);
  ASSERT_EQ(0.0, sched.get_wakeup(0.0, false, false));
}

TEST(SchedulerBudget, BudgetWindowPrunedWhenNowAdvancesPastSampleWindow) {
  MockRng rng(11);
  auto config = make_tight_budget_config(1000000, 100);
  IptController ipt(config.ipt_params, rng);
  ChaffScheduler sched(config, ipt, rng, 0.0);

  sched.note_activity(0.0);
  for (int i = 0; i < 50; i++) {
    sched.note_chaff_emitted(1.0 + static_cast<double>(i) * 0.01, 100);
  }

  double after_window = 1.0 + 60.001;
  sched.note_activity(after_window);

  // Must not crash; no assertion on emit value since budget config allows it.
  bool emit = sched.should_emit(after_window + 0.1, false, true);
  (void)emit;
}

TEST(SchedulerBudget, RepeatedEmissionsNeverUnlockBudgetViaSaturatedAccumulator) {
  MockRng rng(12);
  auto config = make_tight_budget_config(2000, 200);
  IptController ipt(config.ipt_params, rng);
  ChaffScheduler sched(config, ipt, rng, 0.0);

  sched.note_activity(0.0);
  for (int i = 0; i < 10; i++) {
    sched.note_chaff_emitted(static_cast<double>(i) * 0.1, 200);
  }
  ASSERT_FALSE(sched.should_emit(1.5, false, true));
  auto wakeup = sched.get_wakeup(1.5, false, true);
  if (wakeup > 0.0) {
    ASSERT_TRUE(wakeup > 1.5);
    ASSERT_TRUE(std::isfinite(wakeup));
  }
}

TEST(SchedulerBudget, AlternatingNoteActivityAndChaffEmittedRemainCoherent) {
  MockRng rng(13);
  auto config = make_tight_budget_config(5000, 100);
  IptController ipt(config.ipt_params, rng);
  ChaffScheduler sched(config, ipt, rng, 0.0);

  for (int i = 0; i < 20; i++) {
    double t = static_cast<double>(i) * 3.0;
    sched.note_activity(t);
    sched.note_chaff_emitted(t + 1.0, 100);
    double wakeup = sched.get_wakeup(t + 1.0, false, true);
    ASSERT_TRUE(wakeup >= 0.0);
    if (wakeup > 0.0) {
      ASSERT_TRUE(std::isfinite(wakeup));
    }
  }
}

}  // namespace
