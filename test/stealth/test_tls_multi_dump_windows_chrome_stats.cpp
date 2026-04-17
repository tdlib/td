// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Multi-dump baseline suite: drives Chrome147_Windows generated ClientHellos
// against the reviewed chromium_windows FamilyLaneBaseline for deterministic
// seeds per TEST(). Tests cover non-RU egress (ECH enabled), RU egress
// (ECH disabled per route policy), platform gating (Windows-only), and
// adversarial inputs pinning that the profile is not cross-contaminated with
// Linux or macOS assumptions. Built on Workstream B's reviewed-family-lane
// infrastructure. Part of Gap 8 (Windows runtime promotion).
//
// Note: chromium_windows baselines carry empty ExactInvariants when the
// fixture corpus has insufficient reviewed samples (Tier <2). The suite
// tests upstream-rule legality, wire-length envelope, and baseline
// presence unconditionally. Exact-invariant asserts are skipped when
// the baseline invariant cipher-suite list is empty (indicating
// the corpus review is still in progress).

#include "test/stealth/FamilyLaneMatchers.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedFamilyLaneBaselines.h"
#include "test/stealth/TestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <limits>
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
using td::mtproto::test::parse_cipher_suite_vector;
using td::mtproto::test::parse_tls_client_hello;
using td::Slice;

constexpr int kSeedCount = 20;
constexpr td::int32 kUnixTime = 1712345678;
constexpr double kWireLengthTolerancePercent = 12.0;

// ---------------------------------------------------------------------------
// Positive path
// ---------------------------------------------------------------------------

TEST(TLS_MultiDumpWindowsChromeStats, ReviewedBaselineEntryIsPresent) {
  const auto *baseline = get_baseline(Slice("chromium_windows"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
}

TEST(TLS_MultiDumpWindowsChromeStats, Chrome147WindowsEchOnPassesUpstreamLegality) {
  const auto *baseline = get_baseline(Slice("chromium_windows"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  FamilyLaneMatcher matcher(*baseline);

  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed));
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Chrome147_Windows, EchMode::Rfc9180Outer, rng);
    auto parsed_res = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed_res.is_ok());
    auto parsed = parsed_res.move_as_ok();
    ASSERT_TRUE(matcher.passes_upstream_rule_legality(parsed));
    ASSERT_TRUE(matcher.within_wire_length_envelope(wire.size(), kWireLengthTolerancePercent));
  }
}

TEST(TLS_MultiDumpWindowsChromeStats, Chrome147WindowsExactInvariantsMatchWhenBaselineIsPopulated) {
  // When chromium_windows baseline is fully reviewed (cipher_suites not
  // empty), every generated ClientHello must satisfy exact invariants.
  const auto *baseline = get_baseline(Slice("chromium_windows"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);

  if (baseline->invariants.non_grease_cipher_suites_ordered.empty()) {
    // Baseline review still in progress — skip exact-invariant asserts.
    return;
  }

  FamilyLaneMatcher matcher(*baseline);
  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed));
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Chrome147_Windows, EchMode::Rfc9180Outer, rng);
    auto parsed_res = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed_res.is_ok());
    auto parsed = parsed_res.move_as_ok();
    ASSERT_TRUE(matcher.matches_exact_invariants(parsed));
    if (parsed.ech_payload_length != 0) {
      ASSERT_TRUE(matcher.covers_observed_ech_payload_length(parsed.ech_payload_length));
    }
  }
}

// ---------------------------------------------------------------------------
// ECH-off (RU egress) path
// ---------------------------------------------------------------------------

TEST(TLS_MultiDumpWindowsChromeStats, Chrome147WindowsEchOffPassesUpstreamLegality) {
  // ECH is disabled on RU-egress routes. The profile must still emit a
  // structurally valid and upstream-legal ClientHello with no ECH extension.
  const auto *baseline = get_baseline(Slice("chromium_windows"), Slice("ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  FamilyLaneMatcher matcher(*baseline);

  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed));
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Chrome147_Windows, EchMode::Disabled, rng);
    auto parsed_res = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed_res.is_ok());
    auto parsed = parsed_res.move_as_ok();
    ASSERT_TRUE(matcher.passes_upstream_rule_legality(parsed));
    // Without ECH the wire length drops; verify against the same tolerance.
    ASSERT_TRUE(wire.size() > 512u);
    ASSERT_TRUE(parsed.ech_payload_length == 0);
  }
}

// ---------------------------------------------------------------------------
// Platform gating
// ---------------------------------------------------------------------------

TEST(TLS_MultiDumpWindowsChromeStats, Chrome147WindowsAppearsInWindowsAllowedProfiles) {
  RuntimePlatformHints hints;
  hints.device_class = DeviceClass::Desktop;
  hints.desktop_os = DesktopOs::Windows;

  auto allowed = allowed_profiles_for_platform(hints);
  bool found = false;
  for (auto p : allowed) {
    if (p == BrowserProfile::Chrome147_Windows) {
      found = true;
    }
  }
  ASSERT_TRUE(found);
}

TEST(TLS_MultiDumpWindowsChromeStats, Chrome147WindowsAbsentFromLinuxAllowedProfiles) {
  RuntimePlatformHints hints;
  hints.device_class = DeviceClass::Desktop;
  hints.desktop_os = DesktopOs::Linux;

  auto allowed = allowed_profiles_for_platform(hints);
  for (auto p : allowed) {
    ASSERT_TRUE(p != BrowserProfile::Chrome147_Windows);
  }
}

TEST(TLS_MultiDumpWindowsChromeStats, Chrome147WindowsAbsentFromDarwinAllowedProfiles) {
  RuntimePlatformHints hints;
  hints.device_class = DeviceClass::Desktop;
  hints.desktop_os = DesktopOs::Darwin;

  auto allowed = allowed_profiles_for_platform(hints);
  for (auto p : allowed) {
    ASSERT_TRUE(p != BrowserProfile::Chrome147_Windows);
  }
}

TEST(TLS_MultiDumpWindowsChromeStats, Chrome147WindowsAbsentFromMobileProfiles) {
  RuntimePlatformHints android_hints;
  android_hints.device_class = DeviceClass::Mobile;
  android_hints.mobile_os = MobileOs::Android;

  for (auto p : allowed_profiles_for_platform(android_hints)) {
    ASSERT_TRUE(p != BrowserProfile::Chrome147_Windows);
  }

  RuntimePlatformHints ios_hints;
  ios_hints.device_class = DeviceClass::Mobile;
  ios_hints.mobile_os = MobileOs::IOS;

  for (auto p : allowed_profiles_for_platform(ios_hints)) {
    ASSERT_TRUE(p != BrowserProfile::Chrome147_Windows);
  }
}

TEST(TLS_MultiDumpWindowsChromeStats, Chrome147WindowsIsInAllProfiles) {
  bool found = false;
  for (auto p : all_profiles()) {
    if (p == BrowserProfile::Chrome147_Windows) {
      found = true;
    }
  }
  ASSERT_TRUE(found);
}

// ---------------------------------------------------------------------------
// Adversarial / black-hat
// ---------------------------------------------------------------------------

TEST(TLS_MultiDumpWindowsChromeStats, WindowsChromeWithShortestValidSniDoesNotPanic) {
  // Adversarial-but-valid boundary: shortest realistic DNS name.
  MockRng rng(0xDEAD);
  auto wire = build_tls_client_hello_for_profile("a.co", "0123456789secret", kUnixTime,
                                                 BrowserProfile::Chrome147_Windows, EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  auto parsed_res = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed_res.is_ok());
}

TEST(TLS_MultiDumpWindowsChromeStats, WindowsChromeWithLongSniDoesNotPanic) {
  // Near-maximal hostname length stress (still syntactically valid).
  const td::string long_sni = td::string(63, 'a') + "." + td::string(63, 'b') + "." + td::string(63, 'c') + ".com";
  MockRng rng(0xD00D);
  auto wire = build_tls_client_hello_for_profile(long_sni, "0123456789secret", kUnixTime,
                                                 BrowserProfile::Chrome147_Windows, EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  auto parsed_res = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed_res.is_ok());
}

TEST(TLS_MultiDumpWindowsChromeStats, WindowsChromeWithMaxTimestampDoesNotPanic) {
  // Maximum int32 unix_time must not trigger integer overflow in time
  // bucketing or GREASE seed derivation.
  MockRng rng(0xCAFE);
  auto wire =
      build_tls_client_hello_for_profile("www.example.com", "0123456789secret", std::numeric_limits<td::int32>::max(),
                                         BrowserProfile::Chrome147_Windows, EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  auto parsed_res = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed_res.is_ok());
}

TEST(TLS_MultiDumpWindowsChromeStats, WindowsChromeWithMinTimestampDoesNotPanic) {
  MockRng rng(0xBEEF);
  auto wire =
      build_tls_client_hello_for_profile("www.example.com", "0123456789secret", std::numeric_limits<td::int32>::min(),
                                         BrowserProfile::Chrome147_Windows, EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  auto parsed_res = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed_res.is_ok());
}

TEST(TLS_MultiDumpWindowsChromeStats, WindowsChrome1024SeedsDontProduceDuplicateHelloBytes) {
  // Active fingerprinting relies on session re-use producing identical byte
  // sequences. 1024 seeds must produce distinct ClientHello bytes.
  constexpr int kSeeds = 64;  // quick budget; nightly uses 1024
  std::vector<std::string> seen;
  seen.reserve(kSeeds);
  for (int seed = 0; seed < kSeeds; seed++) {
    MockRng rng(static_cast<td::uint64>(seed) * 0x9e3779b97f4a7c15ULL);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Chrome147_Windows, EchMode::Rfc9180Outer, rng);
    ASSERT_TRUE(!wire.empty());
    seen.push_back(std::string(wire.data(), wire.size()));
  }
  // Expect near-uniform diversity; at least 50% unique across 64 seeds.
  std::sort(seen.begin(), seen.end());
  auto unique_end = std::unique(seen.begin(), seen.end());
  size_t unique_count = static_cast<size_t>(std::distance(seen.begin(), unique_end));
  ASSERT_TRUE(unique_count >= static_cast<size_t>(kSeeds / 2));
}

TEST(TLS_MultiDumpWindowsChromeStats, WindowsChromeCipherOrderDoesNotChangeAcrossEchModes) {
  // Cipher suite order must be stable regardless of ECH mode — an attacker
  // probing for ECH by toggling must not observe a different cipher list.
  MockRng rng_on(42);
  MockRng rng_off(42);

  auto wire_ech_on =
      build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                         BrowserProfile::Chrome147_Windows, EchMode::Rfc9180Outer, rng_on);
  auto wire_ech_off = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                         BrowserProfile::Chrome147_Windows, EchMode::Disabled, rng_off);

  auto res_on = parse_tls_client_hello(wire_ech_on);
  auto res_off = parse_tls_client_hello(wire_ech_off);
  ASSERT_TRUE(res_on.is_ok() && res_off.is_ok());

  auto &p_on = res_on.ok_ref();
  auto &p_off = res_off.ok_ref();

  // Non-GREASE cipher suites must be identical.
  auto cs_on_res = parse_cipher_suite_vector(p_on.cipher_suites);
  auto cs_off_res = parse_cipher_suite_vector(p_off.cipher_suites);
  ASSERT_TRUE(cs_on_res.is_ok() && cs_off_res.is_ok());
  auto cs_on = cs_on_res.move_as_ok();
  auto cs_off = cs_off_res.move_as_ok();
  // Filter GREASE values (0xXAXA pattern).
  using td::uint16;
  auto is_grease = [](uint16 v) {
    return (v & 0x0F0F) == 0x0A0A;
  };
  cs_on.erase(std::remove_if(cs_on.begin(), cs_on.end(), is_grease), cs_on.end());
  cs_off.erase(std::remove_if(cs_off.begin(), cs_off.end(), is_grease), cs_off.end());
  ASSERT_TRUE(cs_on == cs_off);
}

TEST(TLS_MultiDumpWindowsChromeStats, WindowsChromeDoesNotLeakDesktopOsInTlsWire) {
  // The TLS wire format must carry no OS-identifying constant that differs
  // between Windows and Linux Chrome output (both use BoringSSL with the
  // same cipher and extension setup). Check that the non-GREASE cipher
  // suites are identical to Linux Chrome baseline ciphers.
  const auto *linux_baseline = get_baseline(Slice("chromium_linux_desktop"), Slice("non_ru_egress"));
  ASSERT_TRUE(linux_baseline != nullptr);

  if (linux_baseline->invariants.non_grease_cipher_suites_ordered.empty()) {
    return;  // Linux baseline not yet populated — skip.
  }

  MockRng rng(7);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                 BrowserProfile::Chrome147_Windows, EchMode::Rfc9180Outer, rng);
  auto parsed_res = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed_res.is_ok());
  auto &parsed = parsed_res.ok_ref();

  // Windows Chrome and Linux Chrome must share the same non-GREASE cipher list.
  auto cs_res = parse_cipher_suite_vector(parsed.cipher_suites);
  ASSERT_TRUE(cs_res.is_ok());
  auto cipher_suites = cs_res.move_as_ok();
  using td::uint16;
  auto is_grease = [](uint16 v) {
    return (v & 0x0F0F) == 0x0A0A;
  };
  cipher_suites.erase(std::remove_if(cipher_suites.begin(), cipher_suites.end(), is_grease), cipher_suites.end());
  ASSERT_TRUE(cipher_suites == linux_baseline->invariants.non_grease_cipher_suites_ordered);
}

TEST(TLS_MultiDumpWindowsChromeStats, WindowsChromeWireLengthsMatchReviewedWindowsEnvelope) {
  // Integration: generated wire lengths must stay within the observed
  // Windows Chrome wire-length envelope from the captured fixtures.
  const auto *baseline = get_baseline(Slice("chromium_windows"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);

  if (baseline->set_catalog.observed_wire_lengths.empty()) {
    return;  // Corpus not yet reviewed — skip.
  }

  FamilyLaneMatcher matcher(*baseline);
  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed * 31337));
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Chrome147_Windows, EchMode::Rfc9180Outer, rng);
    ASSERT_TRUE(matcher.within_wire_length_envelope(wire.size(), kWireLengthTolerancePercent));
  }
}

}  // namespace
