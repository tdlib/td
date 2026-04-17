// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedFamilyLaneBaselines.h"
#include "test/stealth/TestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"
#include "test/stealth/WireClassifierFeatures.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/tests.h"

namespace {

using namespace td;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;

constexpr int32 kUnixTime = 1712345678;

size_t count_non_grease_extensions_without_padding(const ParsedClientHello &hello) {
  size_t count = 0;
  for (const auto &ext : hello.extensions) {
    if (!is_grease_value(ext.type) && ext.type != 0x0015u) {
      count++;
    }
  }
  return count;
}

bool any_template_contains(const vector<vector<uint16>> &templates, uint16 extension_type) {
  for (const auto &templ : templates) {
    if (std::find(templ.begin(), templ.end(), extension_type) != templ.end()) {
      return true;
    }
  }
  return false;
}

TEST(TLS_WireFeatureMaskContract, GeneratedFeaturesExcludePaddingLikeReviewedTemplates) {
  MockRng rng(0x12345678ULL);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                 BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
  auto parsed = parse_tls_client_hello(wire).move_as_ok();
  const auto features = wire_classifier::extract_generated_features(wire);

  ASSERT_EQ(static_cast<double>(count_non_grease_extensions_without_padding(parsed)), features.extension_count);
}

TEST(TLS_WireFeatureMaskContract, ChromiumLinuxSummaryMaskDisablesUnsupportedAlpnCardinality) {
  const auto *baseline = baselines::get_baseline(Slice("chromium_linux_desktop"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  ASSERT_TRUE(baseline->invariants.alpn_protocols.empty());
  ASSERT_TRUE(any_template_contains(baseline->set_catalog.observed_extension_order_templates, 0x0010u));

  const auto mask = wire_classifier::classifier_feature_mask_for_baseline(*baseline);
  ASSERT_FALSE(mask.enabled[wire_classifier::kAlpnCount]);
}

TEST(TLS_WireFeatureMaskContract, EveryLaneMasksAlpnCountWhenSummaryOmitsObservedAlpnCardinality) {
  for (size_t i = 0; i < baselines::get_baseline_count(); i++) {
    const auto &baseline = baselines::get_baseline_by_index(i);
    bool has_observed_alpn_extension =
        any_template_contains(baseline.set_catalog.observed_extension_order_templates, 0x0010u);
    const auto mask = wire_classifier::classifier_feature_mask_for_baseline(baseline);

    if (baseline.invariants.alpn_protocols.empty() && has_observed_alpn_extension) {
      ASSERT_FALSE(mask.enabled[wire_classifier::kAlpnCount]);
    }
  }
}

}  // namespace