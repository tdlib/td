// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Multi-dump baseline suite: drives Safari26_3 / IOS14 generated
// ClientHellos against the reviewed apple_ios_tls and apple_macos_tls
// FamilyLaneBaselines for 20 deterministic seeds per TEST(). These
// Apple lanes pin ech_presence_required=false (no ECH in iOS/macOS
// Safari today), so EchMode::Disabled is the primary mode under test.

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

TEST(TLS_MultiDumpIosAppleTlsBaseline, Ios14NoEchMatchesAppleIosBaseline) {
  const auto *baseline = get_baseline(Slice("apple_ios_tls"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  FamilyLaneMatcher matcher(*baseline);

  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed));
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::IOS14, EchMode::Disabled, rng);
    auto parsed_res = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed_res.is_ok());
    auto parsed = parsed_res.move_as_ok();
    ASSERT_TRUE(matcher.matches_exact_invariants(parsed));
    ASSERT_TRUE(matcher.passes_upstream_rule_legality(parsed));
    ASSERT_TRUE(matcher.within_wire_length_envelope(wire.size(), kWireLengthTolerancePercent));
  }
}

TEST(TLS_MultiDumpIosAppleTlsBaseline, Safari26NoEchMatchesAppleMacosBaseline) {
  const auto *baseline = get_baseline(Slice("apple_macos_tls"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  FamilyLaneMatcher matcher(*baseline);

  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed));
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Safari26_3, EchMode::Disabled, rng);
    auto parsed_res = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed_res.is_ok());
    auto parsed = parsed_res.move_as_ok();
    ASSERT_TRUE(matcher.matches_exact_invariants(parsed));
    ASSERT_TRUE(matcher.passes_upstream_rule_legality(parsed));
    ASSERT_TRUE(matcher.within_wire_length_envelope(wire.size(), kWireLengthTolerancePercent));
  }
}

}  // namespace
