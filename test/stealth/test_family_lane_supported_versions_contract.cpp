// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Contract tests for the non_grease_supported_versions invariant in
// FamilyLaneMatcher::matches_exact_invariants(). Fabricates synthetic
// apple_ios_tls baselines with various supported_versions vectors and
// drives them against a real parsed ClientHello to verify that only the
// correct vector passes and that wrong, empty, or GREASE-only vectors
// are rejected or skipped as expected.

#include "test/stealth/FamilyLaneMatchers.h"

#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedFamilyLaneBaselines.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <algorithm>

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

ParsedClientHello build_and_parse_ios14() {
  MockRng rng(1);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::IOS14, EchMode::Disabled, rng);
  return parse_tls_client_hello(wire).move_as_ok();
}

// Fabricates a minimal apple_ios_tls baseline with the given
// supported_versions invariant. All other invariant fields are left
// empty so the matcher does not reject on an unrelated dimension.
FamilyLaneBaseline make_ios_baseline_with_supported_versions(td::vector<td::uint16> versions) {
  FamilyLaneBaseline b;
  b.family_id = Slice("apple_ios_tls");
  b.route_lane = Slice("non_ru_egress");
  b.tier = TierLevel::Tier2;
  b.raw_tier = TierLevel::Tier2;
  b.sample_count = 3;
  b.authoritative_sample_count = 3;
  b.num_sources = 2;
  b.num_sessions = 3;
  b.stale_over_90_days = false;
  b.stale_over_180_days = false;
  b.invariants.family_id = b.family_id;
  b.invariants.route_lane = b.route_lane;
  b.invariants.non_grease_supported_versions = std::move(versions);
  b.set_catalog.observed_wire_lengths = {512u, 1540u, 1543u};
  return b;
}

// ----------------------------------------------------------------
// Positive: the correct apple_ios_tls supported_versions {TLS 1.3,
// TLS 1.2} must pass the invariant check.
// ----------------------------------------------------------------
TEST(FamilyLaneSupportedVersionsContract, CorrectVersionsPassInvariant) {
  auto baseline = make_ios_baseline_with_supported_versions({0x0304u, 0x0303u});
  FamilyLaneMatcher matcher(baseline);
  ASSERT_TRUE(matcher.matches_exact_invariants(build_and_parse_ios14()));
}

// ----------------------------------------------------------------
// Negative: a supported_versions vector that includes TLS 1.1
// (0x0302) must be rejected.
// ----------------------------------------------------------------
TEST(FamilyLaneSupportedVersionsContract, VectorWithTls11IsRejected) {
  auto baseline = make_ios_baseline_with_supported_versions({0x0304u, 0x0303u, 0x0302u});
  FamilyLaneMatcher matcher(baseline);
  ASSERT_FALSE(matcher.matches_exact_invariants(build_and_parse_ios14()));
}

// ----------------------------------------------------------------
// Negative: a supported_versions vector that includes TLS 1.0
// (0x0301) must be rejected.
// ----------------------------------------------------------------
TEST(FamilyLaneSupportedVersionsContract, VectorWithTls10IsRejected) {
  auto baseline = make_ios_baseline_with_supported_versions({0x0304u, 0x0303u, 0x0301u});
  FamilyLaneMatcher matcher(baseline);
  ASSERT_FALSE(matcher.matches_exact_invariants(build_and_parse_ios14()));
}

// ----------------------------------------------------------------
// Negative: a supported_versions vector that includes both TLS 1.1
// and TLS 1.0 must be rejected.
// ----------------------------------------------------------------
TEST(FamilyLaneSupportedVersionsContract, VectorWithTls11AndTls10IsRejected) {
  auto baseline = make_ios_baseline_with_supported_versions({0x0304u, 0x0303u, 0x0302u, 0x0301u});
  FamilyLaneMatcher matcher(baseline);
  ASSERT_FALSE(matcher.matches_exact_invariants(build_and_parse_ios14()));
}

// ----------------------------------------------------------------
// Edge: an empty supported_versions invariant means "no check" and
// must pass regardless of what the hello advertises.
// ----------------------------------------------------------------
TEST(FamilyLaneSupportedVersionsContract, EmptyInvariantSkipsCheck) {
  auto baseline = make_ios_baseline_with_supported_versions({});
  FamilyLaneMatcher matcher(baseline);
  ASSERT_TRUE(matcher.matches_exact_invariants(build_and_parse_ios14()));
}

// ----------------------------------------------------------------
// Negative: a GREASE-only supported_versions vector must not match
// the real hello which advertises {TLS 1.3, TLS 1.2}. GREASE
// values are stripped from the observed hello by the matcher, so a
// baseline that claims the non-GREASE set is {GREASE} (which is
// effectively empty after stripping) will never equal the observed
// non-GREASE set.
// ----------------------------------------------------------------
TEST(FamilyLaneSupportedVersionsContract, GreaseOnlyVectorIsRejected) {
  // 0x0A0A is a valid GREASE value -- but the matcher compares
  // non-GREASE vectors, so this invariant vector will not match.
  auto baseline = make_ios_baseline_with_supported_versions({0x0A0Au});
  FamilyLaneMatcher matcher(baseline);
  ASSERT_FALSE(matcher.matches_exact_invariants(build_and_parse_ios14()));
}

// ----------------------------------------------------------------
// Negative: reversed version order {TLS 1.2, TLS 1.3} must be
// rejected because supported_versions is order-sensitive.
// ----------------------------------------------------------------
TEST(FamilyLaneSupportedVersionsContract, ReversedVersionOrderIsRejected) {
  auto baseline = make_ios_baseline_with_supported_versions({0x0303u, 0x0304u});
  FamilyLaneMatcher matcher(baseline);
  ASSERT_FALSE(matcher.matches_exact_invariants(build_and_parse_ios14()));
}

// ----------------------------------------------------------------
// Negative: TLS 1.3 alone (missing TLS 1.2) must be rejected
// because the real apple_ios_tls hello advertises both.
// ----------------------------------------------------------------
TEST(FamilyLaneSupportedVersionsContract, Tls13OnlyIsRejected) {
  auto baseline = make_ios_baseline_with_supported_versions({0x0304u});
  FamilyLaneMatcher matcher(baseline);
  ASSERT_FALSE(matcher.matches_exact_invariants(build_and_parse_ios14()));
}

}  // namespace
