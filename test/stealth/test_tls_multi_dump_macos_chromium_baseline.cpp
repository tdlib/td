// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Multi-dump baseline suite for the dedicated macOS Chromium cohorts.
// The reviewed chromium_macos corpus is mixed across no-ALPS, 0x4469,
// and 0x44CD lanes, so runtime promotion must keep those identities
// explicit instead of proxying generic Linux Chrome against macOS data.

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
using td::mtproto::test::baselines::get_baseline;
using td::mtproto::test::find_extension;
using td::mtproto::test::FamilyLaneMatcher;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;

constexpr int kSeedCount = 20;
constexpr td::int32 kUnixTime = 1712345678;
constexpr double kWireLengthTolerancePercent = 12.0;

void assert_profile_matches_macos_chromium_family(BrowserProfile profile, td::uint16 expected_alps_type) {
  const auto *baseline = get_baseline(Slice("chromium_macos"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  FamilyLaneMatcher matcher(*baseline);

  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed));
    auto wire =
        build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile,
                                           EchMode::Rfc9180Outer, rng);
    auto parsed_res = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed_res.is_ok());
    auto parsed = parsed_res.move_as_ok();
    ASSERT_TRUE(matcher.matches_exact_invariants(parsed));
    ASSERT_TRUE(matcher.passes_upstream_rule_legality(parsed));
    ASSERT_TRUE(matcher.within_wire_length_envelope(wire.size(), kWireLengthTolerancePercent));

    auto *alps = find_extension(parsed, expected_alps_type == 0 ? 0x44CDu : expected_alps_type);
    if (expected_alps_type == 0) {
      ASSERT_TRUE(find_extension(parsed, 0x44CDu) == nullptr);
      ASSERT_TRUE(find_extension(parsed, 0x4469u) == nullptr);
    } else {
      ASSERT_TRUE(alps != nullptr);
    }
    if (parsed.ech_payload_length != 0) {
      ASSERT_TRUE(matcher.covers_observed_ech_payload_length(parsed.ech_payload_length));
    }
  }
}

TEST(TLS_MultiDumpMacosChromiumBaseline, ChromiumMacosNoAlpsMatchesMacosChromiumBaseline) {
  assert_profile_matches_macos_chromium_family(BrowserProfile::ChromiumMacOS_NoAlps, 0);
}

TEST(TLS_MultiDumpMacosChromiumBaseline, ChromiumMacos4469MatchesMacosChromiumBaseline) {
  assert_profile_matches_macos_chromium_family(BrowserProfile::ChromiumMacOS_4469, 0x4469u);
}

TEST(TLS_MultiDumpMacosChromiumBaseline, ChromiumMacos44CDMatchesMacosChromiumBaseline) {
  assert_profile_matches_macos_chromium_family(BrowserProfile::ChromiumMacOS_44CD, 0x44CDu);
}

}  // namespace
