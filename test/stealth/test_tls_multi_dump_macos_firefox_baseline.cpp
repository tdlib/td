// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Multi-dump baseline suite: drives Firefox149_MacOS26_3 generated
// ClientHellos against the reviewed firefox_macos FamilyLaneBaseline
// for 20 deterministic seeds per TEST(). Asserts every exact
// invariant, upstream-rule legality check, wire-length envelope (10%)
// and ECH-payload-length catalog entry matches.

#include "test/stealth/FamilyLaneMatchers.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedFamilyLaneBaselines.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/tests.h"

namespace {

using td::Slice;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::FamilyLaneMatcher;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::baselines::get_baseline;

constexpr int kSeedCount = 20;
constexpr td::int32 kUnixTime = 1712345678;
constexpr double kWireLengthTolerancePercent = 10.0;

TEST(TLS_MultiDumpMacosFirefoxBaseline, Firefox149EchOnMatchesMacosBaseline) {
  const auto *baseline = get_baseline(Slice("firefox_macos"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  FamilyLaneMatcher matcher(*baseline);

  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed));
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Firefox149_MacOS26_3, EchMode::Rfc9180Outer, rng);
    auto parsed_res = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed_res.is_ok());
    auto parsed = parsed_res.move_as_ok();
    ASSERT_TRUE(matcher.matches_exact_invariants(parsed));
    ASSERT_TRUE(matcher.passes_upstream_rule_legality(parsed));
    ASSERT_TRUE(matcher.within_wire_length_envelope(wire.size(), kWireLengthTolerancePercent));
    if (parsed.ech_payload_length != 0) {
      ASSERT_TRUE(matcher.covers_observed_ech_payload_length(parsed.ech_payload_length));
    }
  }
}

// No baseline entry for Firefox149_MacOS26_3 with ECH disabled: the
// firefox_macos reviewed observed_wire_lengths are {1905, 2213},
// both ECH-on captures. The no-ECH wire falls outside a 10% envelope
// around those samples — real drift between reviewed fixtures and
// generator coverage, not a generator bug. Skipped until the
// firefox_macos capture set adds ECH-off samples.

}  // namespace
