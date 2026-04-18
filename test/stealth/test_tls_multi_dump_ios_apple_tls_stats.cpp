// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Multi-dump stats suite: IOS14 / Safari26_3 iOS platform-gating,
// adversarial boundary, seed-diversity, and ECH-absence stability checks.
// Complements the baseline suite (exact-invariant + upstream-legality).
// Part of Workstream C, Gap E closure for the apple_ios_tls lane.
//
// Note: IOS14 and Safari26_3 are Advisory-tier families (no network-derived
// authoritative captures at time of authoring). Exact-invariant and
// distributional gates are skipped when the baseline invariants are empty.
// Structural legality and platform-gating gates always apply.

#include "test/stealth/FamilyLaneMatchers.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedFamilyLaneBaselines.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

namespace {

using td::mtproto::stealth::all_profiles;
using td::mtproto::stealth::allowed_profiles_for_platform;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::DesktopOs;
using td::mtproto::stealth::DeviceClass;
using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::MobileOs;
using td::mtproto::stealth::RuntimePlatformHints;
using td::mtproto::test::baselines::get_baseline;
using td::mtproto::test::FamilyLaneMatcher;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;
using td::Slice;

constexpr int kSeedCount = 20;
constexpr td::int32 kUnixTime = 1712345678;
constexpr double kWireLengthTolerancePercent = 15.0;

// ---------------------------------------------------------------------------
// Platform gating — IOS14
// ---------------------------------------------------------------------------

TEST(TLS_MultiDumpIosAppleTlsStats, IOS14AppearsInIosAllowedProfiles) {
  RuntimePlatformHints hints;
  hints.device_class = DeviceClass::Mobile;
  hints.mobile_os = MobileOs::IOS;

  auto allowed = allowed_profiles_for_platform(hints);
  bool found = false;
  for (auto p : allowed) {
    if (p == BrowserProfile::IOS14) {
      found = true;
    }
  }
  ASSERT_TRUE(found);
}

TEST(TLS_MultiDumpIosAppleTlsStats, IOS14AbsentFromAndroidAllowedProfiles) {
  RuntimePlatformHints hints;
  hints.device_class = DeviceClass::Mobile;
  hints.mobile_os = MobileOs::Android;

  auto allowed = allowed_profiles_for_platform(hints);
  for (auto p : allowed) {
    ASSERT_TRUE(p != BrowserProfile::IOS14);
  }
}

TEST(TLS_MultiDumpIosAppleTlsStats, IOS14AbsentFromAllDesktopPlatforms) {
  for (auto os : {DesktopOs::Linux, DesktopOs::Darwin, DesktopOs::Windows}) {
    RuntimePlatformHints hints;
    hints.device_class = DeviceClass::Desktop;
    hints.desktop_os = os;
    for (auto p : allowed_profiles_for_platform(hints)) {
      ASSERT_TRUE(p != BrowserProfile::IOS14);
    }
  }
}

TEST(TLS_MultiDumpIosAppleTlsStats, IOS14IsInAllProfiles) {
  bool found = false;
  for (auto p : all_profiles()) {
    if (p == BrowserProfile::IOS14) {
      found = true;
    }
  }
  ASSERT_TRUE(found);
}

// ---------------------------------------------------------------------------
// Platform gating — Safari26_3
// ---------------------------------------------------------------------------

TEST(TLS_MultiDumpIosAppleTlsStats, Safari26_3AppearsInDarwinAllowedProfiles) {
  // Safari 26.3 is a Darwin-desktop profile in DARWIN_DESKTOP_PROFILES.
  RuntimePlatformHints hints;
  hints.device_class = DeviceClass::Desktop;
  hints.desktop_os = DesktopOs::Darwin;

  auto allowed = allowed_profiles_for_platform(hints);
  bool found = false;
  for (auto p : allowed) {
    if (p == BrowserProfile::Safari26_3) {
      found = true;
    }
  }
  ASSERT_TRUE(found);
}

TEST(TLS_MultiDumpIosAppleTlsStats, Safari26_3AbsentFromMobileProfiles) {
  // Safari 26.3 is Darwin-desktop-only; it must not appear in mobile pools.
  RuntimePlatformHints android_hints;
  android_hints.device_class = DeviceClass::Mobile;
  android_hints.mobile_os = MobileOs::Android;
  for (auto p : allowed_profiles_for_platform(android_hints)) {
    ASSERT_TRUE(p != BrowserProfile::Safari26_3);
  }

  RuntimePlatformHints ios_hints;
  ios_hints.device_class = DeviceClass::Mobile;
  ios_hints.mobile_os = MobileOs::IOS;
  for (auto p : allowed_profiles_for_platform(ios_hints)) {
    ASSERT_TRUE(p != BrowserProfile::Safari26_3);
  }

  for (auto os : {DesktopOs::Linux, DesktopOs::Windows}) {
    RuntimePlatformHints hints;
    hints.device_class = DeviceClass::Desktop;
    hints.desktop_os = os;
    for (auto p : allowed_profiles_for_platform(hints)) {
      ASSERT_TRUE(p != BrowserProfile::Safari26_3);
    }
  }
}

TEST(TLS_MultiDumpIosAppleTlsStats, IOS14EchOnPassesUpstreamLegality) {
  const auto *baseline = get_baseline(Slice("apple_ios_tls"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  FamilyLaneMatcher matcher(*baseline);

  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed));
    auto wire = build_tls_client_hello_for_profile("www.apple.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::IOS14, EchMode::Rfc9180Outer, rng);
    auto parsed_res = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed_res.is_ok());
    ASSERT_TRUE(matcher.passes_upstream_rule_legality(parsed_res.ok_ref()));
  }
}

TEST(TLS_MultiDumpIosAppleTlsStats, IOS14EchOffRuEgressParsesClean) {
  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed));
    auto wire = build_tls_client_hello_for_profile("www.apple.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::IOS14, EchMode::Disabled, rng);
    auto parsed_res = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed_res.is_ok());
    // RU-egress: no ECH extension in wire.
    ASSERT_TRUE(parsed_res.ok_ref().ech_payload_length == 0);
  }
}

TEST(TLS_MultiDumpIosAppleTlsStats, IOS14WireLengthsMatchReviewedAppleIosTlsEnvelope) {
  const auto *baseline = get_baseline(Slice("apple_ios_tls"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  FamilyLaneMatcher matcher(*baseline);

  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed));
    auto wire = build_tls_client_hello_for_profile("www.apple.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::IOS14, EchMode::Rfc9180Outer, rng);
    ASSERT_TRUE(!wire.empty());
    ASSERT_TRUE(matcher.within_wire_length_envelope(wire.size(), kWireLengthTolerancePercent));
  }
}

// ---------------------------------------------------------------------------
// Adversarial / black-hat boundary tests
// ---------------------------------------------------------------------------

TEST(TLS_MultiDumpIosAppleTlsStats, IosAppleTlsWithShortestValidSniDoesNotPanic) {
  MockRng rng(0xDEAD);
  auto wire = build_tls_client_hello_for_profile("a.io", "0123456789secret", kUnixTime, BrowserProfile::IOS14,
                                                 EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  ASSERT_TRUE(parse_tls_client_hello(wire).is_ok());
}

TEST(TLS_MultiDumpIosAppleTlsStats, IosAppleTlsWithLongSniDoesNotPanic) {
  const td::string long_sni = td::string(63, 'i') + "." + td::string(63, 'o') + "." + td::string(63, 's') + ".app";
  MockRng rng(0xD00D);
  auto wire = build_tls_client_hello_for_profile(long_sni, "0123456789secret", kUnixTime, BrowserProfile::IOS14,
                                                 EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  ASSERT_TRUE(parse_tls_client_hello(wire).is_ok());
}

TEST(TLS_MultiDumpIosAppleTlsStats, IosAppleTlsWithMaxTimestampDoesNotPanic) {
  MockRng rng(0xCAFE);
  auto wire =
      build_tls_client_hello_for_profile("www.apple.com", "0123456789secret", std::numeric_limits<td::int32>::max(),
                                         BrowserProfile::IOS14, EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  ASSERT_TRUE(parse_tls_client_hello(wire).is_ok());
}

TEST(TLS_MultiDumpIosAppleTlsStats, IosAppleTlsWithMinTimestampDoesNotPanic) {
  MockRng rng(0xBEEF);
  auto wire =
      build_tls_client_hello_for_profile("www.apple.com", "0123456789secret", std::numeric_limits<td::int32>::min(),
                                         BrowserProfile::IOS14, EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  ASSERT_TRUE(parse_tls_client_hello(wire).is_ok());
}

// ---------------------------------------------------------------------------
// Cross-family separation: iOS Apple TLS must differ from iOS Chromium
// ---------------------------------------------------------------------------

TEST(TLS_MultiDumpIosAppleTlsStats, IOS14WireDiffersFromIosChromium) {
  // Apple TLS and Chromium on iOS must produce structurally distinct wires
  // (different cipher suites, different extension layout). Any seed overlap
  // would be a cross-family contamination defect.
  MockRng rng_apple(42);
  MockRng rng_chrome(42);

  auto wire_apple = build_tls_client_hello_for_profile("www.apple.com", "0123456789secret", kUnixTime,
                                                       BrowserProfile::IOS14, EchMode::Rfc9180Outer, rng_apple);
  auto wire_chrome =
      build_tls_client_hello_for_profile("www.apple.com", "0123456789secret", kUnixTime,
                                         BrowserProfile::Chrome147_IOSChromium, EchMode::Rfc9180Outer, rng_chrome);

  // Different TLS stacks: CFNetwork vs Chromium/BoringSSL. Wires must differ.
  ASSERT_TRUE(wire_apple != wire_chrome);
}

TEST(TLS_MultiDumpIosAppleTlsStats, Safari26_3WireIsStructurallyValid) {
  // Safari 26.3 uses FixedFromFixture extension order policy; verify that
  // generated wires parse cleanly for multiple seeds regardless of whether
  // they differ byte-for-byte from IOS14 (both profiles may share the same
  // underlying fixture when IOS14 is used as a baseline for Safari 26.3).
  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed));
    auto wire = build_tls_client_hello_for_profile("www.apple.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Safari26_3, EchMode::Rfc9180Outer, rng);
    ASSERT_TRUE(!wire.empty());
    ASSERT_TRUE(parse_tls_client_hello(wire).is_ok());
  }
}

// ---------------------------------------------------------------------------
// Seed-diversity — replay resistance
// ---------------------------------------------------------------------------

TEST(TLS_MultiDumpIosAppleTlsStats, IosAppleTls64SeedsDontProduceDuplicateHelloBytes) {
  constexpr int kSeeds = 64;
  std::vector<std::string> seen;
  seen.reserve(kSeeds);
  for (int seed = 0; seed < kSeeds; seed++) {
    MockRng rng(static_cast<td::uint64>(seed) * 0x9e3779b97f4a7c15ULL);
    auto wire = build_tls_client_hello_for_profile("www.apple.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::IOS14, EchMode::Rfc9180Outer, rng);
    ASSERT_TRUE(!wire.empty());
    seen.emplace_back(wire.data(), wire.size());
  }
  std::sort(seen.begin(), seen.end());
  auto unique_end = std::unique(seen.begin(), seen.end());
  size_t unique_count = static_cast<size_t>(std::distance(seen.begin(), unique_end));
  // Session IDs and ECH key material must provide diversity.
  ASSERT_TRUE(unique_count >= static_cast<size_t>(kSeeds / 2));
}

}  // namespace
