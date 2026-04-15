// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Multi-dump baseline suite: drives Chrome131 / Chrome133 Linux-desktop
// generated ClientHellos against the reviewed chromium_linux_desktop
// FamilyLaneBaseline for 20 deterministic seeds per TEST(). Every exact
// invariant, upstream-rule legality check, wire-length envelope (10%)
// and ECH-payload-length catalog entry must match for every seed. Built
// on top of Workstream B's reviewed-family-lane infrastructure.

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

void run_linux_chrome_multi_dump(BrowserProfile profile, EchMode ech_mode) {
  const auto *baseline = get_baseline(Slice("chromium_linux_desktop"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  FamilyLaneMatcher matcher(*baseline);

  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed));
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime, profile, ech_mode,
                                                   rng);
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

TEST(TLS_MultiDumpLinuxChromeBaseline, Chrome133EchOnMatchesLinuxDesktopBaseline) {
  run_linux_chrome_multi_dump(BrowserProfile::Chrome133, EchMode::Rfc9180Outer);
}

TEST(TLS_MultiDumpLinuxChromeBaseline, Chrome131EchOnMatchesLinuxDesktopBaseline) {
  run_linux_chrome_multi_dump(BrowserProfile::Chrome131, EchMode::Rfc9180Outer);
}

TEST(TLS_MultiDumpLinuxChromeBaseline, Chrome133EchOffPassesUpstreamLegality) {
  // With ECH disabled the generator emits a no-ECH ClientHello. The
  // chromium_linux_desktop baseline pins `ech_presence_required=true`,
  // so matches_exact_invariants is intentionally not asserted here —
  // instead we pin the upstream-rule legality + wire-length envelope,
  // which both hold for the no-ECH variant as well.
  const auto *baseline = get_baseline(Slice("chromium_linux_desktop"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  FamilyLaneMatcher matcher(*baseline);

  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed));
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    auto parsed_res = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed_res.is_ok());
    auto parsed = parsed_res.move_as_ok();
    ASSERT_TRUE(matcher.passes_upstream_rule_legality(parsed));
    ASSERT_TRUE(matcher.within_wire_length_envelope(wire.size(), kWireLengthTolerancePercent));
  }
}

}  // namespace
