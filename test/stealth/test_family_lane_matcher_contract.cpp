// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Contract tests for FamilyLaneMatcher. Fabricates a small synthetic
// FamilyLaneBaseline and drives it against the output of the real
// ClientHello builder + parser to check positive/negative assertions.

#include "test/stealth/FamilyLaneMatchers.h"

#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedFamilyLaneBaselines.h"
#include "test/stealth/TestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::baselines::FamilyLaneBaseline;
using td::mtproto::test::baselines::TierLevel;
using td::mtproto::test::FamilyLaneMatcher;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::ParsedClientHello;
using td::Slice;
using td::string;

ParsedClientHello build_and_parse_chrome133() {
  MockRng rng(1);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
  return parse_tls_client_hello(wire).move_as_ok();
}

ParsedClientHello build_and_parse_firefox148() {
  MockRng rng(1);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Firefox148, EchMode::Rfc9180Outer, rng);
  return parse_tls_client_hello(wire).move_as_ok();
}

FamilyLaneBaseline make_synthetic_baseline_for_chrome133() {
  // Fabricated baseline: enforce Chrome133 cipher-suite order, ECH
  // presence, and the observed payload-length bucket set. Leaves the
  // extension_set empty so we do not hardcode shuffle-dependent data.
  FamilyLaneBaseline b;
  b.family_id = Slice("chromium_linux_desktop");
  b.route_lane = Slice("non_ru_egress");
  b.tier = TierLevel::Tier2;
  b.sample_count = 3;
  b.num_sources = 3;
  b.invariants.family_id = b.family_id;
  b.invariants.route_lane = b.route_lane;
  b.invariants.non_grease_cipher_suites_ordered = {0x1301u, 0x1302u, 0x1303u, 0xC02Bu, 0xC02Fu,
                                                   0xC02Cu, 0xC030u, 0xCCA9u, 0xCCA8u, 0xC013u,
                                                   0xC014u, 0x009Cu, 0x009Du, 0x002Fu, 0x0035u};
  b.invariants.non_grease_supported_groups = {0x11ECu, 0x001Du, 0x0017u, 0x0018u};
  b.invariants.compress_cert_algorithms = {0x0002u};
  b.invariants.alpn_protocols = {"h2", "http/1.1"};
  b.invariants.ech_presence_required = true;
  b.invariants.tls_record_version = 0x0301u;
  b.invariants.client_hello_legacy_version = 0x0303u;
  b.set_catalog.observed_wire_lengths = {1500u, 1600u, 1700u};
  b.set_catalog.observed_ech_payload_lengths = {144u, 176u, 208u, 240u};
  b.set_catalog.observed_alps_types = {0x44CDu};
  // Observed extension templates — we only add one template matching a
  // typical Chrome shuffle; the matcher should return false for any
  // other ordering.
  b.set_catalog.observed_extension_order_templates = {
      {0x0000u, 0x002Bu, 0x0033u, 0x000Du, 0x0005u, 0xFE0Du, 0x44CDu, 0x0010u},
  };
  return b;
}

TEST(FamilyLaneMatcherContract, ChromeHelloSatisfiesSyntheticInvariants) {
  auto baseline = make_synthetic_baseline_for_chrome133();
  FamilyLaneMatcher matcher(baseline);
  ASSERT_TRUE(matcher.matches_exact_invariants(build_and_parse_chrome133()));
}

TEST(FamilyLaneMatcherContract, FirefoxHelloFailsChromeInvariants) {
  auto baseline = make_synthetic_baseline_for_chrome133();
  FamilyLaneMatcher matcher(baseline);
  // Firefox 148 has 17 non-GREASE cipher suites and no 0x44CD; it must
  // not pass a Chrome-pinned exact-invariant check.
  ASSERT_FALSE(matcher.matches_exact_invariants(build_and_parse_firefox148()));
}

TEST(FamilyLaneMatcherContract, ChromeHelloPassesUpstreamRuleLegality) {
  auto baseline = make_synthetic_baseline_for_chrome133();
  FamilyLaneMatcher matcher(baseline);
  ASSERT_TRUE(matcher.passes_upstream_rule_legality(build_and_parse_chrome133()));
}

TEST(FamilyLaneMatcherContract, EchPayloadLengthCoverageContract) {
  auto baseline = make_synthetic_baseline_for_chrome133();
  FamilyLaneMatcher matcher(baseline);
  ASSERT_TRUE(matcher.covers_observed_ech_payload_length(144));
  ASSERT_TRUE(matcher.covers_observed_ech_payload_length(240));
  ASSERT_FALSE(matcher.covers_observed_ech_payload_length(239));
  ASSERT_FALSE(matcher.covers_observed_ech_payload_length(500));
}

TEST(FamilyLaneMatcherContract, WireLengthEnvelopeContract) {
  auto baseline = make_synthetic_baseline_for_chrome133();
  FamilyLaneMatcher matcher(baseline);
  // 1% tolerance: 1500 +/- 15 must pass, 1600 +/- 16 must pass, 2000
  // must fail.
  ASSERT_TRUE(matcher.within_wire_length_envelope(1500u, 1.0));
  ASSERT_TRUE(matcher.within_wire_length_envelope(1510u, 1.0));
  ASSERT_FALSE(matcher.within_wire_length_envelope(1400u, 1.0));
  ASSERT_FALSE(matcher.within_wire_length_envelope(2000u, 1.0));
}

TEST(FamilyLaneMatcherContract, ExtensionOrderTemplateCoverageContract) {
  auto baseline = make_synthetic_baseline_for_chrome133();
  FamilyLaneMatcher matcher(baseline);
  td::vector<td::uint16> known = {0x0000u, 0x002Bu, 0x0033u, 0x000Du, 0x0005u, 0xFE0Du, 0x44CDu, 0x0010u};
  ASSERT_TRUE(matcher.covers_observed_extension_order_template(known));
  td::vector<td::uint16> unknown = {0x0000u, 0x0033u, 0x002Bu, 0x000Du, 0x0005u, 0xFE0Du, 0x44CDu, 0x0010u};
  ASSERT_FALSE(matcher.covers_observed_extension_order_template(unknown));
}

TEST(FamilyLaneMatcherContract, ReviewedTableHasChromiumLinuxDesktopLane) {
  const auto *b = td::mtproto::test::baselines::get_baseline(Slice("chromium_linux_desktop"), Slice("non_ru_egress"));
  ASSERT_TRUE(b != nullptr);
  ASSERT_TRUE(b->sample_count > 0u);
  // Real reviewed table: wire-length envelope check against a Chrome
  // capture size should succeed within 5%.
  FamilyLaneMatcher matcher(*b);
  ASSERT_TRUE(matcher.within_wire_length_envelope(1779u, 5.0) || matcher.within_wire_length_envelope(1780u, 5.0) ||
              matcher.within_wire_length_envelope(1500u, 5.0));
}

TEST(FamilyLaneMatcherContract, ReviewedTableByIndexCountMatches) {
  auto count = td::mtproto::test::baselines::get_baseline_count();
  ASSERT_TRUE(count > 0u);
  for (size_t i = 0; i < count; i++) {
    const auto &b = td::mtproto::test::baselines::get_baseline_by_index(i);
    ASSERT_FALSE(b.family_id.empty());
    ASSERT_FALSE(b.route_lane.empty());
    if (b.sample_count == 0u) {
      ASSERT_TRUE(b.authoritative_sample_count == 0u);
      ASSERT_TRUE(b.set_catalog.observed_wire_lengths.empty());
      ASSERT_TRUE(b.set_catalog.observed_extension_order_templates.empty());
    } else {
      ASSERT_TRUE(b.set_catalog.observed_wire_lengths.size() > 0u);
    }
  }
}

}  // namespace
