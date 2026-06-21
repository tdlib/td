// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/BackportTestSeams.h"

#include "td/utils/tests.h"

namespace {

TEST(VideoAlternativePropertiesRepairRuntime, CommonAlternativeDurationRepairsZeroPrimaryDuration) {
  std::vector<td::AlternativeVideoRepairCandidate> alternatives{{15, false}, {15, true}, {0, false}};

  auto plan = td::get_alternative_video_repair_plan(0, false, alternatives);

  ASSERT_EQ(15, plan.repaired_duration);
  ASSERT_TRUE(plan.needs_alternative_thumbnail_scan);
}

TEST(VideoAlternativePropertiesRepairRuntime, ConflictingAlternativeDurationsDoNotInventPrimaryDuration) {
  std::vector<td::AlternativeVideoRepairCandidate> alternatives{{12, false}, {18, true}};

  auto plan = td::get_alternative_video_repair_plan(0, false, alternatives);

  ASSERT_EQ(0, plan.repaired_duration);
  ASSERT_TRUE(plan.needs_alternative_thumbnail_scan);
}

TEST(VideoAlternativePropertiesRepairRuntime, ExistingPrimaryThumbnailSkipsAlternativeThumbnailScan) {
  std::vector<td::AlternativeVideoRepairCandidate> alternatives{{12, true}};

  auto plan = td::get_alternative_video_repair_plan(0, true, alternatives);

  ASSERT_EQ(12, plan.repaired_duration);
  ASSERT_FALSE(plan.needs_alternative_thumbnail_scan);
}

}  // namespace
