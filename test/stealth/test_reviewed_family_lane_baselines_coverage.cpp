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

constexpr const char *kFamilies[] = {
    "android_chromium", "apple_ios_tls",    "apple_macos_tls", "chromium_linux_desktop",
    "chromium_macos",   "chromium_windows", "firefox_android", "firefox_linux_desktop",
    "firefox_macos",    "firefox_windows",  "ios_chromium",
};

constexpr const char *kRouteLanes[] = {"non_ru_egress", "ru_egress", "unknown"};

TEST(ReviewedFamilyLaneBaselinesCoverage, EveryExpectedLaneIsPresent) {
  for (const auto *family : kFamilies) {
    for (const auto *lane : kRouteLanes) {
      const auto *baseline = td::mtproto::test::baselines::get_baseline(Slice(family), Slice(lane));
      if (baseline == nullptr) {
        LOG(ERROR) << "Missing reviewed baseline entry for family_id='" << family << "' route_lane='" << lane << "'";
      }
      ASSERT_TRUE(baseline != nullptr);
      ASSERT_EQ(Slice(family), baseline->family_id);
      ASSERT_EQ(Slice(lane), baseline->route_lane);
    }
  }
}

TEST(ReviewedFamilyLaneBaselinesCoverage, TableSizeMatchesExpectedLaneCount) {
  constexpr size_t kExpectedCount =
      (sizeof(kFamilies) / sizeof(kFamilies[0])) * (sizeof(kRouteLanes) / sizeof(kRouteLanes[0]));
  ASSERT_EQ(kExpectedCount, td::mtproto::test::baselines::get_baseline_count());
}

TEST(ReviewedFamilyLaneBaselinesCoverage, TierAssignmentsMatchSampleThresholds) {
  using td::mtproto::test::baselines::TierLevel;

  auto expected_raw_tier = [](size_t authoritative_count, size_t num_sources, size_t num_sessions) {
    if (authoritative_count == 0u) {
      return TierLevel::Tier0;
    }
    if (authoritative_count >= 200u && num_sources >= 3u && num_sessions >= 2u) {
      return TierLevel::Tier4;
    }
    if (authoritative_count >= 15u && num_sources >= 3u && num_sessions >= 2u) {
      return TierLevel::Tier3;
    }
    if (authoritative_count >= 3u && num_sources >= 2u && num_sessions >= 2u) {
      return TierLevel::Tier2;
    }
    return TierLevel::Tier1;
  };

  auto expected_effective_tier = [](TierLevel raw_tier, bool stale_over_180_days) {
    if (!stale_over_180_days) {
      return raw_tier;
    }
    if (raw_tier == TierLevel::Tier4) {
      return TierLevel::Tier3;
    }
    if (raw_tier == TierLevel::Tier3) {
      return TierLevel::Tier2;
    }
    if (raw_tier == TierLevel::Tier2) {
      return TierLevel::Tier1;
    }
    return raw_tier;
  };

  for (size_t i = 0; i < td::mtproto::test::baselines::get_baseline_count(); i++) {
    const auto &baseline = td::mtproto::test::baselines::get_baseline_by_index(i);
    const auto expected_raw =
        expected_raw_tier(baseline.authoritative_sample_count, baseline.num_sources, baseline.num_sessions);
    const auto expected_tier = expected_effective_tier(expected_raw, baseline.stale_over_180_days);
    if (baseline.raw_tier != expected_raw || baseline.tier != expected_tier) {
      LOG(ERROR) << "Unexpected tier for family_id='" << baseline.family_id << "' route_lane='" << baseline.route_lane
                 << "' authoritative_sample_count=" << baseline.authoritative_sample_count
                 << " num_sources=" << baseline.num_sources << " num_sessions=" << baseline.num_sessions
                 << " stale_over_180_days=" << baseline.stale_over_180_days;
    }
    ASSERT_TRUE(baseline.raw_tier == expected_raw);
    ASSERT_TRUE(baseline.tier == expected_tier);
  }
}

TEST(ReviewedFamilyLaneBaselinesCoverage, EveryLaneHasAtLeastOneObservedWireLength) {
  for (size_t i = 0; i < td::mtproto::test::baselines::get_baseline_count(); i++) {
    const auto &baseline = td::mtproto::test::baselines::get_baseline_by_index(i);
    if (baseline.route_lane == Slice("non_ru_egress")) {
      ASSERT_FALSE(baseline.set_catalog.observed_wire_lengths.empty());
    } else {
      ASSERT_TRUE(baseline.set_catalog.observed_wire_lengths.empty());
    }
  }
}

TEST(ReviewedFamilyLaneBaselinesCoverage, EveryLaneHasAtLeastOneOrderTemplate) {
  for (size_t i = 0; i < td::mtproto::test::baselines::get_baseline_count(); i++) {
    const auto &baseline = td::mtproto::test::baselines::get_baseline_by_index(i);
    if (baseline.route_lane == Slice("non_ru_egress")) {
      ASSERT_FALSE(baseline.set_catalog.observed_extension_order_templates.empty());
    } else {
      ASSERT_TRUE(baseline.set_catalog.observed_extension_order_templates.empty());
    }
  }
}

TEST(ReviewedFamilyLaneBaselinesCoverage, FailClosedLanesAreTierZeroAndEchOff) {
  using td::mtproto::test::baselines::TierLevel;

  for (const auto *family : kFamilies) {
    const auto *ru = td::mtproto::test::baselines::get_baseline(Slice(family), Slice("ru_egress"));
    const auto *unknown = td::mtproto::test::baselines::get_baseline(Slice(family), Slice("unknown"));
    ASSERT_TRUE(ru != nullptr);
    ASSERT_TRUE(unknown != nullptr);

    ASSERT_TRUE(ru->tier == TierLevel::Tier0);
    ASSERT_TRUE(ru->raw_tier == TierLevel::Tier0);
    ASSERT_EQ(0u, ru->sample_count);
    ASSERT_EQ(0u, ru->authoritative_sample_count);
    ASSERT_FALSE(ru->invariants.ech_presence_required);
    ASSERT_TRUE(ru->set_catalog.observed_ech_payload_lengths.empty());

    ASSERT_TRUE(ru->tier == unknown->tier);
    ASSERT_TRUE(ru->raw_tier == unknown->raw_tier);
    ASSERT_EQ(ru->sample_count, unknown->sample_count);
    ASSERT_EQ(ru->invariants.ech_presence_required, unknown->invariants.ech_presence_required);
    ASSERT_EQ(ru->set_catalog.observed_ech_payload_lengths, unknown->set_catalog.observed_ech_payload_lengths);
  }
}

}  // namespace
