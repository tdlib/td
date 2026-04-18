// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Security regression: FamilyLaneMatcher must fail closed when the baseline
// carries an unknown family_id. This prevents silent acceptance if corpus
// classification drifts or a family label is mistyped.

#include "test/stealth/FamilyLaneMatchers.h"

#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedFamilyLaneBaselines.h"
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
using td::Slice;

TEST(FamilyLaneMatcherFailClosed, UnknownFamilyRejectsLegalityForOtherwiseValidHello) {
  FamilyLaneBaseline baseline;
  baseline.family_id = Slice("unknown_family_lane");
  baseline.route_lane = Slice("non_ru_egress");
  baseline.tier = TierLevel::Tier1;
  baseline.raw_tier = TierLevel::Tier1;
  baseline.sample_count = 1;
  baseline.authoritative_sample_count = 1;
  baseline.num_sources = 1;
  baseline.num_sessions = 1;

  FamilyLaneMatcher matcher(baseline);

  MockRng rng(7);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  ASSERT_FALSE(matcher.passes_upstream_rule_legality(parsed.ok_ref()));
}

}  // namespace
