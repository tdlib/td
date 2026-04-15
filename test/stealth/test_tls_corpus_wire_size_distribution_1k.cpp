// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/CorpusIterationTiers.h"
#include "test/stealth/CorpusStatHelpers.h"
#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using namespace td;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;

const uint64 kCorpusIterations = spot_or_full_corpus_iterations();
constexpr int32 kUnixTime = 1712345678;
const uint32 kMinimumSizeCoverage = static_cast<uint32>(kCorpusIterations / 8);
const uint32 kHalfRunThreshold = static_cast<uint32>(kCorpusIterations / 2);

string build_hello(BrowserProfile profile, EchMode ech_mode, uint64 seed) {
  MockRng rng(corpus_seed_for_iteration(seed, kCorpusIterations));
  return build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile, ech_mode, rng);
}

FrequencyCounter<size_t> size_counter(BrowserProfile profile, EchMode ech_mode) {
  FrequencyCounter<size_t> counter;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    counter.count(build_hello(profile, ech_mode, seed).size());
  }
  return counter;
}

TEST(WireSizeDistribution1k, Chrome133EchEnabledHasExactlyFourDistinctSizes) {
  auto counter = size_counter(BrowserProfile::Chrome133, EchMode::Rfc9180Outer);
  ASSERT_TRUE(counter.distinct_values() == 4u);
}

TEST(WireSizeDistribution1k, Chrome133EchEnabledDistributionClearsConservativeFloor) {
  auto counter = size_counter(BrowserProfile::Chrome133, EchMode::Rfc9180Outer);
  for (const auto &it : counter.counts()) {
    ASSERT_TRUE(it.second >= kMinimumSizeCoverage);
  }
}

TEST(WireSizeDistribution1k, Chrome133EchDisabledHasAtLeastSevenDistinctSizes) {
  auto counter = size_counter(BrowserProfile::Chrome133, EchMode::Disabled);
  ASSERT_TRUE(counter.distinct_values() >= 7u);
}

TEST(WireSizeDistribution1k, Chrome133EchDisabledHasNoDominantSize) {
  auto counter = size_counter(BrowserProfile::Chrome133, EchMode::Disabled);
  ASSERT_TRUE(counter.max_observed() <= kHalfRunThreshold);
}

TEST(WireSizeDistribution1k, Firefox148HasFixedWireSizeWithAndWithoutEch) {
  ASSERT_TRUE(size_counter(BrowserProfile::Firefox148, EchMode::Rfc9180Outer).distinct_values() == 1u);
  ASSERT_TRUE(size_counter(BrowserProfile::Firefox148, EchMode::Disabled).distinct_values() == 1u);
}

TEST(WireSizeDistribution1k, Safari26_3AndIos14HaveFixedWireSizesWithoutEch) {
  ASSERT_TRUE(size_counter(BrowserProfile::Safari26_3, EchMode::Disabled).distinct_values() == 1u);
  ASSERT_TRUE(size_counter(BrowserProfile::IOS14, EchMode::Disabled).distinct_values() == 1u);
}

TEST(WireSizeDistribution1k, AllProfilesAvoidLegacyFixed517Length) {
  for (auto profile : all_profiles()) {
    for (auto ech_mode : {EchMode::Disabled, EchMode::Rfc9180Outer}) {
      if (ech_mode == EchMode::Rfc9180Outer && !profile_spec(profile).allows_ech) {
        continue;
      }
      for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
        ASSERT_TRUE(build_hello(profile, ech_mode, seed).size() != 517u);
      }
    }
  }
}

TEST(WireSizeDistribution1k, Chrome133WireSizesStayWithinSanityBounds) {
  auto enabled = size_counter(BrowserProfile::Chrome133, EchMode::Rfc9180Outer);
  auto disabled = size_counter(BrowserProfile::Chrome133, EchMode::Disabled);
  for (const auto &it : enabled.counts()) {
    ASSERT_TRUE(it.first > 200u);
    ASSERT_TRUE(it.first < 16000u);
  }
  for (const auto &it : disabled.counts()) {
    ASSERT_TRUE(it.first > 200u);
    ASSERT_TRUE(it.first < 16000u);
  }
}

TEST(WireSizeDistribution1k, Chrome133EchEnabledHasNarrowerSizeSetThanDisabledLane) {
  auto enabled = size_counter(BrowserProfile::Chrome133, EchMode::Rfc9180Outer);
  auto disabled = size_counter(BrowserProfile::Chrome133, EchMode::Disabled);
  ASSERT_TRUE(enabled.distinct_values() < disabled.distinct_values());
}

}  // namespace