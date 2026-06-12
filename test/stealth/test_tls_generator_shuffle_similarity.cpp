// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Chrome shuffle similarity, separating anchored-shuffle *legality* and
// *diversity* from real-corpus *evidence*:
//   - Shuffling families (chromium): generated orders must be legal anchored
//     permutations, must not collapse to a degenerate set of sequences, must
//     contain no duplicated extension types, and must use an extension set that
//     was actually observed in the reviewed corpus.
//   - Fixed-order families (apple_ios_tls): generated order must equal the
//     single reviewed template and must not spuriously shuffle.
//
// Deviation from the drafted plan, by data truth (per TDD_approach sec 4.4 --
// the code is correct, the drafted assertion was wrong): the plan compared the
// generated set to `baseline->invariants.non_grease_extension_set` and assumed
// it could be made `Exact`. The chromium_linux_desktop cohort pools Chrome
// builds whose extension set genuinely differs (reviewed sizes 15/16/17 -- ECH
// and ALPS presence vary across sources), so by the plan's own Exact rule
// ("every reviewed sample has the same value") that field is a per-template
// Catalog, and its collapsed exact invariant is correctly empty. Forcing it to
// Exact would fabricate evidence. We instead require each generated set to
// equal the set of some reviewed order template -- a set actually seen in a
// real dump -- which is the fixture-derived check the plan intended.

#include "test/stealth/CorpusStatHelpers.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedFamilyLaneBaselines.h"
#include "test/stealth/TlsHelloParsers.h"
#include "test/stealth/UpstreamRuleVerifiers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/tests.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace {

using td::Slice;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::MockRng;
using td::mtproto::test::baselines::get_baseline;
using td::mtproto::test::extension_set_non_grease_no_padding;
using td::mtproto::test::hex_u16;
using td::mtproto::test::non_grease_extension_sequence;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::verifiers::ExtensionOrderVerifier;

constexpr td::int32 kUnixTime = 1712345678;

const Slice kChromiumLinuxDesktop("chromium_linux_desktop");
const Slice kNonRuEgress("non_ru_egress");

std::string order_key(const std::vector<td::uint16> &order) {
  std::string result;
  for (auto value : order) {
    if (!result.empty()) {
      result.push_back(',');
    }
    result += hex_u16(value);
  }
  return result;
}

std::vector<td::uint16> sorted_unique(std::vector<td::uint16> values) {
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end()), values.end());
  return values;
}

TEST(TlsGeneratorShuffleSimilarity, ChromiumLinuxReviewedCorpusHasMultipleObservedTemplates) {
  const auto *baseline = get_baseline(kChromiumLinuxDesktop, kNonRuEgress);
  ASSERT_TRUE(baseline != nullptr);
  ASSERT_TRUE(baseline->set_catalog.observed_extension_order_templates.size() > 1u);
}

TEST(TlsGeneratorShuffleSimilarity, Chrome133GeneratedOrdersAreLegalAndUseReviewedExtensionSet) {
  const auto *baseline = get_baseline(kChromiumLinuxDesktop, kNonRuEgress);
  ASSERT_TRUE(baseline != nullptr);

  // Reviewed extension sets, one per observed order template (deduplicated).
  const auto &templates = baseline->set_catalog.observed_extension_order_templates;
  ASSERT_FALSE(templates.empty());
  std::vector<std::vector<td::uint16>> reviewed_sets;
  reviewed_sets.reserve(templates.size());
  for (const auto &templ : templates) {
    reviewed_sets.push_back(sorted_unique(templ));
  }

  const auto &verifier = ExtensionOrderVerifier::get_for_family(kChromiumLinuxDesktop);
  std::set<std::string> distinct_orders;
  for (td::uint64 seed = 0; seed < 512; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());

    auto order = non_grease_extension_sequence(parsed.ok_ref());
    ASSERT_TRUE(verifier.is_legal_permutation(order));

    auto observed_set = extension_set_non_grease_no_padding(parsed.ok_ref());
    // No duplicated extension types: ordered sequence and deduplicated set agree.
    ASSERT_EQ(order.size(), observed_set.size());

    std::vector<td::uint16> observed(observed_set.begin(), observed_set.end());
    std::sort(observed.begin(), observed.end());
    ASSERT_TRUE(std::find(reviewed_sets.begin(), reviewed_sets.end(), observed) != reviewed_sets.end());

    distinct_orders.insert(order_key(order));
  }
  ASSERT_TRUE(distinct_orders.size() >= 256u);
}

TEST(TlsGeneratorShuffleSimilarity, FixedOrderFamiliesMatchReviewedTemplateInsteadOfShufflePolicy) {
  const auto *baseline = get_baseline(Slice("apple_ios_tls"), kNonRuEgress);
  ASSERT_TRUE(baseline != nullptr);
  ASSERT_EQ(1u, baseline->set_catalog.observed_extension_order_templates.size());

  std::set<std::string> distinct_orders;
  for (td::uint64 seed = 0; seed < 64; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.apple.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::IOS14, EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    auto order = non_grease_extension_sequence(parsed.ok_ref());
    ASSERT_EQ(baseline->set_catalog.observed_extension_order_templates[0], order);
    distinct_orders.insert(order_key(order));
  }
  ASSERT_EQ(1u, distinct_orders.size());
}

}  // namespace
