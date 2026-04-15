// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Coverage regression: the generated ReviewedFamilyLaneBaselines.h must
// contain an entry for every (family_id, route_lane) pair that appears in
// the frozen ClientHello capture corpus under
// test/analysis/fixtures/clienthello/**/*.clienthello.json. The
// byte-level staleness regression is handled by the Python generator's
// own --self-test harness; this test enforces the expected membership
// from the C++ side so accidental hand-edits to the header are caught
// alongside fixture additions.

#include "test/stealth/ReviewedFamilyLaneBaselines.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::Slice;

// Keep this list in sync with the Python classifier in
// test/analysis/build_family_lane_baselines.py::classify_family_id and
// with the fixtures currently committed under
// test/analysis/fixtures/clienthello. If a new family or route lane is
// introduced, regenerate the header and add the pair here.
struct ExpectedLane {
  const char *family_id;
  const char *route_lane;
};

constexpr ExpectedLane kExpectedLanes[] = {
    {"android_chromium", "non_ru_egress"},
    {"apple_ios_tls", "non_ru_egress"},
    {"apple_macos_tls", "non_ru_egress"},
    {"chromium_linux_desktop", "non_ru_egress"},
    {"chromium_macos", "non_ru_egress"},
    {"chromium_windows", "non_ru_egress"},
    {"firefox_android", "non_ru_egress"},
    {"firefox_linux_desktop", "non_ru_egress"},
    {"firefox_macos", "non_ru_egress"},
    {"firefox_windows", "non_ru_egress"},
    {"ios_chromium", "non_ru_egress"},
};

TEST(ReviewedFamilyLaneBaselinesCoverage, EveryExpectedLaneIsPresent) {
  for (const auto &expected : kExpectedLanes) {
    const auto *baseline = td::mtproto::test::baselines::get_baseline(
        Slice(expected.family_id), Slice(expected.route_lane));
    if (baseline == nullptr) {
      LOG(ERROR) << "Missing reviewed baseline entry for family_id='" << expected.family_id
                 << "' route_lane='" << expected.route_lane << "'";
    }
    ASSERT_TRUE(baseline != nullptr);
    ASSERT_EQ(Slice(expected.family_id), baseline->family_id);
    ASSERT_EQ(Slice(expected.route_lane), baseline->route_lane);
    ASSERT_TRUE(baseline->sample_count > 0u);
  }
}

TEST(ReviewedFamilyLaneBaselinesCoverage, TableSizeMatchesExpectedLaneCount) {
  constexpr size_t kExpectedCount = sizeof(kExpectedLanes) / sizeof(kExpectedLanes[0]);
  ASSERT_EQ(kExpectedCount, td::mtproto::test::baselines::get_baseline_count());
}

TEST(ReviewedFamilyLaneBaselinesCoverage, TierAssignmentsMatchSampleThresholds) {
  using td::mtproto::test::baselines::TierLevel;
  for (size_t i = 0; i < td::mtproto::test::baselines::get_baseline_count(); i++) {
    const auto &baseline = td::mtproto::test::baselines::get_baseline_by_index(i);
    auto n = baseline.sample_count;
    TierLevel expected_tier = TierLevel::Tier1;
    if (n >= 200) {
      expected_tier = TierLevel::Tier4;
    } else if (n >= 15) {
      expected_tier = TierLevel::Tier3;
    } else if (n >= 3) {
      expected_tier = TierLevel::Tier2;
    }
    if (baseline.tier != expected_tier) {
      LOG(ERROR) << "Unexpected tier for family_id='" << baseline.family_id
                 << "' route_lane='" << baseline.route_lane << "' sample_count=" << n;
    }
    ASSERT_TRUE(baseline.tier == expected_tier);
  }
}

TEST(ReviewedFamilyLaneBaselinesCoverage, EveryLaneHasAtLeastOneObservedWireLength) {
  for (size_t i = 0; i < td::mtproto::test::baselines::get_baseline_count(); i++) {
    const auto &baseline = td::mtproto::test::baselines::get_baseline_by_index(i);
    ASSERT_FALSE(baseline.set_catalog.observed_wire_lengths.empty());
  }
}

TEST(ReviewedFamilyLaneBaselinesCoverage, EveryLaneHasAtLeastOneOrderTemplate) {
  for (size_t i = 0; i < td::mtproto::test::baselines::get_baseline_count(); i++) {
    const auto &baseline = td::mtproto::test::baselines::get_baseline_by_index(i);
    ASSERT_FALSE(baseline.set_catalog.observed_extension_order_templates.empty());
  }
}

}  // namespace
