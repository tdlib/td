// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/ReviewedFamilyLaneBaselines.h"
#include "test/stealth/WireClassifierFeatures.h"

#include "td/utils/tests.h"

#include <set>

namespace {

using td::mtproto::test::baselines::get_baseline;
using td::mtproto::test::wire_classifier::load_real_features_for_family_lane;
using td::Slice;

TEST(TLS_WireRealFixtureFeaturesContract, LoadsOneFeatureTuplePerAuthoritativeCapture) {
  const auto *baseline = get_baseline(Slice("chromium_linux_desktop"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  ASSERT_TRUE(baseline->authoritative_sample_count >= 15u);

  auto result = load_real_features_for_family_lane(Slice("chromium_linux_desktop"), Slice("non_ru_egress"));
  ASSERT_TRUE(result.is_ok());
  const auto samples = result.move_as_ok();

  ASSERT_EQ(baseline->authoritative_sample_count, samples.size());
}

TEST(TLS_WireRealFixtureFeaturesContract, RejectsFailClosedLanesWithoutReviewedSamples) {
  auto result = load_real_features_for_family_lane(Slice("chromium_linux_desktop"), Slice("ru_egress"));
  ASSERT_TRUE(result.is_error());
}

TEST(TLS_WireRealFixtureFeaturesContract, PreservesReviewedWireLengthCatalogMembership) {
  const auto *baseline = get_baseline(Slice("chromium_linux_desktop"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);

  auto result = load_real_features_for_family_lane(Slice("chromium_linux_desktop"), Slice("non_ru_egress"));
  ASSERT_TRUE(result.is_ok());
  const auto samples = result.move_as_ok();

  std::set<size_t> observed_wire_lengths;
  for (const auto &sample : samples) {
    observed_wire_lengths.insert(static_cast<size_t>(sample.wire_length));
  }

  std::set<size_t> baseline_wire_lengths(baseline->set_catalog.observed_wire_lengths.begin(),
                                         baseline->set_catalog.observed_wire_lengths.end());
  ASSERT_EQ(baseline_wire_lengths.size(), observed_wire_lengths.size());
  ASSERT_TRUE(std::equal(baseline_wire_lengths.begin(), baseline_wire_lengths.end(), observed_wire_lengths.begin(),
                         observed_wire_lengths.end()));
}

TEST(TLS_WireRealFixtureFeaturesContract, PreservesObservedFeatureHeterogeneityFromReviewedCorpus) {
  auto result = load_real_features_for_family_lane(Slice("chromium_linux_desktop"), Slice("non_ru_egress"));
  ASSERT_TRUE(result.is_ok());
  const auto samples = result.move_as_ok();

  std::set<size_t> extension_counts;
  std::set<size_t> alpn_counts;
  for (const auto &sample : samples) {
    extension_counts.insert(static_cast<size_t>(sample.extension_count));
    alpn_counts.insert(static_cast<size_t>(sample.alpn_count));
  }

  ASSERT_TRUE(extension_counts.size() >= 3u);
  ASSERT_TRUE(alpn_counts.size() >= 2u);
}

}  // namespace