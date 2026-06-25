// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/BackportTestSeams.h"

#include "td/utils/tests.h"

#include <algorithm>
#include <random>

namespace {

td::AlternativeVideoRepairPlan get_expected_plan(
    td::int32 primary_duration, bool has_primary_thumbnail,
    const std::vector<td::AlternativeVideoRepairCandidate> &alternatives) {
  td::int32 consensus_duration = 0;
  bool conflicting_positive_durations = false;
  bool has_alternative_thumbnail = false;

  for (const auto &alternative : alternatives) {
    if (alternative.duration > 0) {
      if (consensus_duration == 0) {
        consensus_duration = alternative.duration;
      } else if (consensus_duration != alternative.duration) {
        conflicting_positive_durations = true;
      }
    }
    has_alternative_thumbnail = has_alternative_thumbnail || alternative.has_thumbnail;
  }

  if (conflicting_positive_durations) {
    consensus_duration = 0;
  }

  td::int32 repaired_duration = primary_duration;
  if (repaired_duration == 0 && consensus_duration > 0) {
    repaired_duration = consensus_duration;
  }

  return {repaired_duration, !has_primary_thumbnail && has_alternative_thumbnail};
}

TEST(VideoAlternativePropertiesRepairLightFuzz, RandomizedAlternativeSetsMatchExpectedRepairRules) {
  std::mt19937 rng(0xA17F87C4u);
  std::uniform_int_distribution<td::int32> primary_duration_dist(0, 20);
  std::uniform_int_distribution<td::int32> alternative_count_dist(0, 8);
  std::uniform_int_distribution<td::int32> alternative_duration_dist(0, 20);
  std::bernoulli_distribution bool_dist(0.5);

  for (std::size_t iteration = 0; iteration < 10000; iteration++) {
    auto primary_duration = primary_duration_dist(rng);
    auto has_primary_thumbnail = bool_dist(rng);

    std::vector<td::AlternativeVideoRepairCandidate> alternatives;
    auto alternative_count = alternative_count_dist(rng);
    alternatives.reserve(static_cast<std::size_t>(alternative_count));
    for (td::int32 i = 0; i < alternative_count; i++) {
      alternatives.push_back({alternative_duration_dist(rng), bool_dist(rng)});
    }

    auto plan = td::get_alternative_video_repair_plan(primary_duration, has_primary_thumbnail, alternatives);
    auto expected = get_expected_plan(primary_duration, has_primary_thumbnail, alternatives);

    ASSERT_EQ(expected.repaired_duration, plan.repaired_duration);
    ASSERT_EQ(expected.needs_alternative_thumbnail_scan, plan.needs_alternative_thumbnail_scan);
    ASSERT_TRUE(plan.repaired_duration >= 0);
    if (primary_duration > 0) {
      ASSERT_EQ(primary_duration, plan.repaired_duration);
    }
  }
}

TEST(VideoAlternativePropertiesRepairLightFuzz, RepairPlanIsPermutationInvariant) {
  std::mt19937 rng(0x1A8D2417u);
  std::uniform_int_distribution<td::int32> primary_duration_dist(0, 20);
  std::uniform_int_distribution<td::int32> alternative_count_dist(1, 8);
  std::uniform_int_distribution<td::int32> alternative_duration_dist(0, 20);
  std::bernoulli_distribution bool_dist(0.5);

  for (std::size_t iteration = 0; iteration < 10000; iteration++) {
    auto primary_duration = primary_duration_dist(rng);
    auto has_primary_thumbnail = bool_dist(rng);

    std::vector<td::AlternativeVideoRepairCandidate> alternatives;
    auto alternative_count = alternative_count_dist(rng);
    alternatives.reserve(static_cast<std::size_t>(alternative_count));
    for (td::int32 i = 0; i < alternative_count; i++) {
      alternatives.push_back({alternative_duration_dist(rng), bool_dist(rng)});
    }

    auto baseline = td::get_alternative_video_repair_plan(primary_duration, has_primary_thumbnail, alternatives);
    std::shuffle(alternatives.begin(), alternatives.end(), rng);
    auto shuffled = td::get_alternative_video_repair_plan(primary_duration, has_primary_thumbnail, alternatives);

    ASSERT_EQ(baseline.repaired_duration, shuffled.repaired_duration);
    ASSERT_EQ(baseline.needs_alternative_thumbnail_scan, shuffled.needs_alternative_thumbnail_scan);
  }
}

}  // namespace
