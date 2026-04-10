// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/StealthRecordSizeBaselines.h"

#include "td/utils/tests.h"

#include <cstddef>

namespace {

using td::int32;
using td::mtproto::stealth::baselines::kActiveBrowsingBins;
using td::mtproto::stealth::baselines::kBulkTransferBins;
using td::mtproto::stealth::baselines::kGreetingRecord1;
using td::mtproto::stealth::baselines::kGreetingRecord2;
using td::mtproto::stealth::baselines::kGreetingRecord3;
using td::mtproto::stealth::baselines::kGreetingRecord4;
using td::mtproto::stealth::baselines::kGreetingRecord5;
using td::mtproto::stealth::baselines::kIdleChaffBins;
using td::mtproto::stealth::baselines::kSmallRecordMaxFraction;
using td::mtproto::stealth::baselines::kSmallRecordThreshold;
using td::mtproto::stealth::RecordSizeBin;
using td::mtproto::test::MockRng;
using td::uint32;

template <size_t N>
constexpr size_t count_of(const RecordSizeBin (&)[N]) {
  return N;
}

template <size_t N>
bool bins_are_non_overlapping(const RecordSizeBin (&bins)[N]) {
  for (size_t i = 1; i < N; i++) {
    if (bins[i - 1].hi >= bins[i].lo) {
      return false;
    }
  }
  return true;
}

template <size_t N>
bool weights_are_positive(const RecordSizeBin (&bins)[N]) {
  for (const auto &bin : bins) {
    if (bin.weight == 0) {
      return false;
    }
  }
  return true;
}

template <size_t N>
int32 sample_from_bins(const RecordSizeBin (&bins)[N], MockRng &rng) {
  uint32 total_weight = 0;
  for (const auto &bin : bins) {
    total_weight += bin.weight;
  }
  CHECK(total_weight != 0);
  auto pick = rng.bounded(total_weight);
  const RecordSizeBin *chosen = &bins[0];
  uint32 seen = 0;
  for (const auto &bin : bins) {
    seen += bin.weight;
    if (pick < seen) {
      chosen = &bin;
      break;
    }
  }
  auto span = static_cast<uint32>(chosen->hi - chosen->lo + 1);
  return chosen->lo + static_cast<int32>(rng.bounded(span));
}

TEST(StealthRecordSizeBaselines, BaselineBinsNonEmpty) {
  ASSERT_TRUE(count_of(kGreetingRecord1) > 0u);
  ASSERT_TRUE(count_of(kGreetingRecord2) > 0u);
  ASSERT_TRUE(count_of(kGreetingRecord3) > 0u);
  ASSERT_TRUE(count_of(kGreetingRecord4) > 0u);
  ASSERT_TRUE(count_of(kGreetingRecord5) > 0u);
  ASSERT_TRUE(count_of(kActiveBrowsingBins) > 0u);
  ASSERT_TRUE(count_of(kBulkTransferBins) > 0u);
  ASSERT_TRUE(count_of(kIdleChaffBins) > 0u);
}

TEST(StealthRecordSizeBaselines, BaselineBinsNonOverlapping) {
  ASSERT_TRUE(bins_are_non_overlapping(kGreetingRecord1));
  ASSERT_TRUE(bins_are_non_overlapping(kGreetingRecord2));
  ASSERT_TRUE(bins_are_non_overlapping(kGreetingRecord3));
  ASSERT_TRUE(bins_are_non_overlapping(kGreetingRecord4));
  ASSERT_TRUE(bins_are_non_overlapping(kGreetingRecord5));
  ASSERT_TRUE(bins_are_non_overlapping(kActiveBrowsingBins));
  ASSERT_TRUE(bins_are_non_overlapping(kBulkTransferBins));
  ASSERT_TRUE(bins_are_non_overlapping(kIdleChaffBins));
}

TEST(StealthRecordSizeBaselines, BaselineBinWeightsPositive) {
  ASSERT_TRUE(weights_are_positive(kGreetingRecord1));
  ASSERT_TRUE(weights_are_positive(kGreetingRecord2));
  ASSERT_TRUE(weights_are_positive(kGreetingRecord3));
  ASSERT_TRUE(weights_are_positive(kGreetingRecord4));
  ASSERT_TRUE(weights_are_positive(kGreetingRecord5));
  ASSERT_TRUE(weights_are_positive(kActiveBrowsingBins));
  ASSERT_TRUE(weights_are_positive(kBulkTransferBins));
  ASSERT_TRUE(weights_are_positive(kIdleChaffBins));
}

TEST(StealthRecordSizeBaselines, BaselineGreetingCoversFirstFlight) {
  ASSERT_TRUE(kGreetingRecord1[0].lo >= 80);
  ASSERT_TRUE(kGreetingRecord1[count_of(kGreetingRecord1) - 1].hi <= 1500);
  ASSERT_TRUE(kGreetingRecord2[0].lo >= 80);
  ASSERT_TRUE(kGreetingRecord2[count_of(kGreetingRecord2) - 1].hi <= 1500);
  ASSERT_TRUE(kGreetingRecord3[0].lo >= 80);
  ASSERT_TRUE(kGreetingRecord3[count_of(kGreetingRecord3) - 1].hi <= 1500);
  ASSERT_TRUE(kGreetingRecord4[0].lo >= 80);
  ASSERT_TRUE(kGreetingRecord4[count_of(kGreetingRecord4) - 1].hi <= 1500);
  ASSERT_TRUE(kGreetingRecord5[0].lo >= 80);
  ASSERT_TRUE(kGreetingRecord5[count_of(kGreetingRecord5) - 1].hi <= 1500);
}

TEST(StealthRecordSizeBaselines, BaselineBulkIncludesNear16K) {
  ASSERT_TRUE(kBulkTransferBins[count_of(kBulkTransferBins) - 2].lo <= 16401);
  ASSERT_TRUE(kBulkTransferBins[count_of(kBulkTransferBins) - 1].hi >= 16408);
}

TEST(StealthRecordSizeBaselines, BaselineSmallRecordBudgetSane) {
  ASSERT_TRUE(kSmallRecordThreshold >= 200);
  ASSERT_TRUE(kSmallRecordMaxFraction > 0.0);
  ASSERT_TRUE(kSmallRecordMaxFraction <= 0.50);
}

TEST(StealthRecordSizeBaselines, SamplingFromBaselineProducesWeightedActiveDistribution) {
  MockRng rng(42);
  size_t low = 0;
  size_t mid = 0;
  size_t upper_mid = 0;
  size_t tail = 0;
  for (size_t i = 0; i < 4096; i++) {
    auto sampled = sample_from_bins(kActiveBrowsingBins, rng);
    if (sampled <= 494) {
      low++;
    } else if (sampled <= 2941) {
      mid++;
    } else if (sampled <= 5394) {
      upper_mid++;
    } else {
      tail++;
    }
  }
  ASSERT_TRUE(mid > low);
  ASSERT_TRUE(mid > tail);
  ASSERT_TRUE(upper_mid > tail);
}

TEST(StealthRecordSizeBaselines, SamplingNeverExceedsTlsMax) {
  MockRng rng(7);
  for (size_t i = 0; i < 2048; i++) {
    ASSERT_TRUE(sample_from_bins(kActiveBrowsingBins, rng) <= 16640);
    ASSERT_TRUE(sample_from_bins(kBulkTransferBins, rng) <= 16640);
    ASSERT_TRUE(sample_from_bins(kIdleChaffBins, rng) <= 16640);
  }
}

TEST(StealthRecordSizeBaselines, SamplingNeverBelowZero) {
  MockRng rng(8);
  for (size_t i = 0; i < 2048; i++) {
    ASSERT_TRUE(sample_from_bins(kGreetingRecord1, rng) > 0);
    ASSERT_TRUE(sample_from_bins(kGreetingRecord2, rng) > 0);
    ASSERT_TRUE(sample_from_bins(kGreetingRecord3, rng) > 0);
    ASSERT_TRUE(sample_from_bins(kGreetingRecord4, rng) > 0);
    ASSERT_TRUE(sample_from_bins(kGreetingRecord5, rng) > 0);
  }
}

}  // namespace