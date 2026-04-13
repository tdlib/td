// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/CorpusStatHelpers.h"
#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <set>

namespace {

using namespace td;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;
using namespace td::mtproto::test::fixtures;

const uint64 kCorpusIterations = spot_or_full_corpus_iterations();
const uint64 kJa3DiversityFloor = is_nightly_corpus_enabled() ? 256u : 32u;
constexpr int32 kUnixTime = 1712345678;

uint64 quick_seed(uint64 iteration_index) {
  return corpus_seed_for_iteration(iteration_index, kQuickIterations);
}

uint64 corpus_seed(uint64 iteration_index) {
  return corpus_seed_for_iteration(iteration_index, kCorpusIterations);
}

string build_profile_hello(BrowserProfile profile, EchMode ech_mode, uint64 seed) {
  MockRng rng(seed);
  return build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile, ech_mode, rng);
}

Ja4Segments compute_ja4_for_wire(Slice wire) {
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  return compute_ja4_segments(parsed.ok());
}

TEST(JA3JA4CorpusStability1k, Chrome133EchJa3ShowsLargeDiversityAcross1024Seeds) {
  std::set<string> values;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    values.insert(compute_ja3(build_profile_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, corpus_seed(seed))));
  }
  ASSERT_TRUE(values.size() >= kJa3DiversityFloor);
}

TEST(JA3JA4CorpusStability1k, Chrome133EchJa3IsNotKnownTelegramHash) {
  auto ja3 = compute_ja3(build_profile_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, 7));
  ASSERT_NE(string(kKnownTelegramJa3), ja3);
}

TEST(JA3JA4CorpusStability1k, Chrome131EchJa3ShowsLargeDiversityAcross1024Seeds) {
  std::set<string> values;
  for (uint64 seed = 0; seed < kCorpusIterations; seed++) {
    values.insert(compute_ja3(build_profile_hello(BrowserProfile::Chrome131, EchMode::Rfc9180Outer, corpus_seed(seed))));
  }
  ASSERT_TRUE(values.size() >= kJa3DiversityFloor);
}

TEST(JA3JA4CorpusStability1k, Chrome133Ja3DiffersFromChrome131Ja3) {
  auto chrome131 = compute_ja3(build_profile_hello(BrowserProfile::Chrome131, EchMode::Rfc9180Outer, 13));
  auto chrome133 = compute_ja3(build_profile_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, 13));
  ASSERT_NE(chrome131, chrome133);
}

TEST(JA3JA4CorpusStability1k, Firefox148EchJa3IsIdenticalAcross1024Seeds) {
  std::set<string> values;
  for (uint64 seed = 0; seed < kQuickIterations; seed++) {
    values.insert(compute_ja3(build_profile_hello(BrowserProfile::Firefox148, EchMode::Rfc9180Outer, quick_seed(seed))));
  }
  ASSERT_EQ(1u, values.size());
}

TEST(JA3JA4CorpusStability1k, Firefox148Ja3DiffersFromChrome133Ja3) {
  auto firefox = compute_ja3(build_profile_hello(BrowserProfile::Firefox148, EchMode::Rfc9180Outer, 21));
  auto chrome = compute_ja3(build_profile_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, 21));
  ASSERT_NE(firefox, chrome);
}

TEST(JA3JA4CorpusStability1k, Chrome133Ja4SegmentBIsIdenticalAcross1024Seeds) {
  std::set<string> values;
  for (uint64 seed = 0; seed < kQuickIterations; seed++) {
    values.insert(
        compute_ja4_for_wire(build_profile_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, quick_seed(seed)))
            .segment_b);
  }
  ASSERT_EQ(1u, values.size());
}

TEST(JA3JA4CorpusStability1k, Chrome133Ja4SegmentCEchEnabledIsIdenticalAcross1024Seeds) {
  std::set<string> values;
  for (uint64 seed = 0; seed < kQuickIterations; seed++) {
    values.insert(
        compute_ja4_for_wire(build_profile_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, quick_seed(seed)))
            .segment_c);
  }
  ASSERT_EQ(1u, values.size());
}

TEST(JA3JA4CorpusStability1k, Chrome133Ja4SegmentCEchDisabledIsStableAcrossSeeds) {
  std::set<string> values;
  for (uint64 seed = 0; seed < kQuickIterations; seed++) {
    values.insert(
        compute_ja4_for_wire(build_profile_hello(BrowserProfile::Chrome133, EchMode::Disabled, quick_seed(seed)))
            .segment_c);
  }
  ASSERT_EQ(1u, values.size());
}

TEST(JA3JA4CorpusStability1k, Chrome133Ja4SegmentCEchEnabledDiffersFromDisabled) {
  auto enabled = compute_ja4_for_wire(build_profile_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, 55));
  auto disabled = compute_ja4_for_wire(build_profile_hello(BrowserProfile::Chrome133, EchMode::Disabled, 55));
  ASSERT_NE(enabled.segment_c, disabled.segment_c);
}

TEST(JA3JA4CorpusStability1k, Chrome133Ja4SegmentAEncodesTls13AndH2) {
  auto segments = compute_ja4_for_wire(build_profile_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, 89));
  ASSERT_EQ(string("t13"), segments.segment_a.substr(0, 3));
  ASSERT_EQ(string("h2"), segments.segment_a.substr(segments.segment_a.size() - 2));
}

TEST(JA3JA4CorpusStability1k, Firefox148Ja4SegmentCIsIdenticalAcross1024Seeds) {
  std::set<string> values;
  for (uint64 seed = 0; seed < kQuickIterations; seed++) {
    values.insert(
        compute_ja4_for_wire(build_profile_hello(BrowserProfile::Firefox148, EchMode::Rfc9180Outer, quick_seed(seed)))
            .segment_c);
  }
  ASSERT_EQ(1u, values.size());
}

TEST(JA3JA4CorpusStability1k, AllProfilesJa3DoNotMatchKnownTelegramHash) {
  for (auto profile :
       {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Chrome120, BrowserProfile::Firefox148,
        BrowserProfile::Safari26_3, BrowserProfile::IOS14, BrowserProfile::Android11_OkHttp_Advisory}) {
    for (uint64 seed = 0; seed < 10; seed++) {
      auto ech_mode = profile == BrowserProfile::Firefox148 || profile == BrowserProfile::Chrome133 ||
                              profile == BrowserProfile::Chrome131
                          ? EchMode::Rfc9180Outer
                          : EchMode::Disabled;
      ASSERT_NE(string(kKnownTelegramJa3), compute_ja3(build_profile_hello(profile, ech_mode, seed)));
    }
  }
}

TEST(JA3JA4CorpusStability1k, Firefox148Ja4SegmentBDiffersFromChrome133Ja4SegmentB) {
  auto firefox = compute_ja4_for_wire(build_profile_hello(BrowserProfile::Firefox148, EchMode::Rfc9180Outer, 144));
  auto chrome = compute_ja4_for_wire(build_profile_hello(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, 144));
  ASSERT_NE(firefox.segment_b, chrome.segment_b);
}

}  // namespace