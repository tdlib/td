// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Release-facing extension-count similarity gate. The reviewed dumps for a
// family/lane yield a non-GREASE, non-padding extension-count histogram
// (a Catalog of observed counts). This suite drives the generator over many
// seeds and requires every emitted ClientHello's extension count to appear in
// that reviewed Catalog, so a generator that drifts to a count never seen in
// real browsers fails the gate instead of being silently accepted by a broad
// envelope. The count metric (GREASE excluded, padding 0x0015 excluded)
// matches the metric the generator script uses to build the histogram.
//
// ECH mode per family follows the reviewed ech_presence_required, which also
// fixes the expected count: chromium (Chrome133 + Rfc9180Outer) -> 16,
// firefox (Firefox148 + Rfc9180Outer) -> 17, apple_ios_tls (IOS14 + Disabled,
// no ECH) -> 13. Each lands in its reviewed histogram Catalog.

#include "test/stealth/CorpusStatHelpers.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedFamilyLaneBaselines.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/tests.h"

namespace {

using td::Slice;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::MockRng;
using td::mtproto::test::baselines::EvidenceFieldStatus;
using td::mtproto::test::baselines::ExtensionCountBucket;
using td::mtproto::test::baselines::get_baseline;
using td::mtproto::test::extension_set_non_grease_no_padding;
using td::mtproto::test::parse_tls_client_hello;

constexpr td::int32 kUnixTime = 1712345678;
constexpr td::uint64 kSeeds = 128;

bool histogram_contains_count(const td::vector<ExtensionCountBucket> &histogram, size_t count) {
  for (const auto &bucket : histogram) {
    if (bucket.count == count) {
      return true;
    }
  }
  return false;
}

void run_extension_count_gate(Slice family_id, BrowserProfile profile, EchMode ech_mode) {
  const auto *baseline = get_baseline(family_id, Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  ASSERT_TRUE(baseline->non_grease_extension_count_histogram_status == EvidenceFieldStatus::Catalog);
  ASSERT_FALSE(baseline->non_grease_extension_count_histogram.empty());

  for (td::uint64 seed = 0; seed < kSeeds; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile, ech_mode,
                                                   rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    auto count = extension_set_non_grease_no_padding(parsed.ok_ref()).size();
    ASSERT_TRUE(histogram_contains_count(baseline->non_grease_extension_count_histogram, count));
  }
}

TEST(TlsGeneratorExtensionCountSimilarity, Chrome133CountsAppearInReviewedChromiumLinuxCatalog) {
  run_extension_count_gate(Slice("chromium_linux_desktop"), BrowserProfile::Chrome133, EchMode::Rfc9180Outer);
}

TEST(TlsGeneratorExtensionCountSimilarity, Firefox148CountsAppearInReviewedFirefoxLinuxCatalog) {
  run_extension_count_gate(Slice("firefox_linux_desktop"), BrowserProfile::Firefox148, EchMode::Rfc9180Outer);
}

TEST(TlsGeneratorExtensionCountSimilarity, IOS14CountsAppearInReviewedAppleIosCatalog) {
  run_extension_count_gate(Slice("apple_ios_tls"), BrowserProfile::IOS14, EchMode::Disabled);
}

}  // namespace
