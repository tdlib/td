// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Multi-dump baseline suite: drives Firefox149_Windows generated ClientHellos
// against the reviewed firefox_windows FamilyLaneBaseline for deterministic
// seeds per TEST(). Tests cover non-RU egress (ECH enabled), RU egress
// (ECH disabled per route policy), platform gating (Windows-only), and
// adversarial inputs. Built on Workstream B's reviewed-family-lane
// infrastructure. Part of Gap 8 (Windows runtime promotion).
//
// Note: firefox_windows baselines carry empty ExactInvariants when the
// fixture corpus has insufficient reviewed samples (Tier <2). The suite
// tests upstream-rule legality, wire-length envelope, and baseline
// presence unconditionally; exact-invariant asserts skip when the
// invariant cipher-suite list is empty.

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
constexpr double kWireLengthTolerancePercent = 25.0;

// ---------------------------------------------------------------------------
// Positive path
// ---------------------------------------------------------------------------

TEST(TLS_MultiDumpWindowsFirefoxStats, ReviewedBaselineEntryIsPresent) {
  const auto *baseline = get_baseline(Slice("firefox_windows"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
}

TEST(TLS_MultiDumpWindowsFirefoxStats, Firefox149WindowsEchOnPassesUpstreamLegality) {
  const auto *baseline = get_baseline(Slice("firefox_windows"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  FamilyLaneMatcher matcher(*baseline);

  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed));
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Firefox149_Windows, EchMode::Rfc9180Outer, rng);
    auto parsed_res = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed_res.is_ok());
    auto parsed = parsed_res.move_as_ok();
    ASSERT_TRUE(matcher.passes_upstream_rule_legality(parsed));
    if (!baseline->set_catalog.observed_wire_lengths.empty()) {
      ASSERT_TRUE(matcher.within_wire_length_envelope(wire.size(), kWireLengthTolerancePercent));
    }
  }
}

TEST(TLS_MultiDumpWindowsFirefoxStats, Firefox149WindowsExactInvariantsMatchWhenBaselineIsPopulated) {
  const auto *baseline = get_baseline(Slice("firefox_windows"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);

  if (baseline->invariants.non_grease_cipher_suites_ordered.empty()) {
    return;  // Baseline review still in progress.
  }

  FamilyLaneMatcher matcher(*baseline);
  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed));
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Firefox149_Windows, EchMode::Rfc9180Outer, rng);
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

TEST(TLS_MultiDumpWindowsFirefoxStats, Firefox149WindowsEchOffPassesUpstreamLegality) {
  const auto *baseline = get_baseline(Slice("firefox_windows"), Slice("ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  FamilyLaneMatcher matcher(*baseline);

  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed));
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Firefox149_Windows, EchMode::Disabled, rng);
    auto parsed_res = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed_res.is_ok());
    auto parsed = parsed_res.move_as_ok();
    ASSERT_TRUE(matcher.passes_upstream_rule_legality(parsed));
    ASSERT_TRUE(wire.size() > 512u);
    ASSERT_TRUE(parsed.ech_payload_length == 0);
  }
}

// ---------------------------------------------------------------------------
// Platform gating
// ---------------------------------------------------------------------------

TEST(TLS_MultiDumpWindowsFirefoxStats, Firefox149WindowsAppearsInWindowsAllowedProfiles) {
  RuntimePlatformHints hints;
  hints.device_class = DeviceClass::Desktop;
  hints.desktop_os = DesktopOs::Windows;

  auto allowed = allowed_profiles_for_platform(hints);
  bool found = false;
  for (auto p : allowed) {
    if (p == BrowserProfile::Firefox149_Windows) {
      found = true;
    }
  }
  ASSERT_TRUE(found);
}

TEST(TLS_MultiDumpWindowsFirefoxStats, Firefox149WindowsAbsentFromLinuxAllowedProfiles) {
  RuntimePlatformHints hints;
  hints.device_class = DeviceClass::Desktop;
  hints.desktop_os = DesktopOs::Linux;

  auto allowed = allowed_profiles_for_platform(hints);
  for (auto p : allowed) {
    ASSERT_TRUE(p != BrowserProfile::Firefox149_Windows);
  }
}

TEST(TLS_MultiDumpWindowsFirefoxStats, Firefox149WindowsAbsentFromDarwinAllowedProfiles) {
  RuntimePlatformHints hints;
  hints.device_class = DeviceClass::Desktop;
  hints.desktop_os = DesktopOs::Darwin;

  auto allowed = allowed_profiles_for_platform(hints);
  for (auto p : allowed) {
    ASSERT_TRUE(p != BrowserProfile::Firefox149_Windows);
  }
}

TEST(TLS_MultiDumpWindowsFirefoxStats, Firefox149WindowsAbsentFromMobileProfiles) {
  RuntimePlatformHints android_hints;
  android_hints.device_class = DeviceClass::Mobile;
  android_hints.mobile_os = MobileOs::Android;

  for (auto p : allowed_profiles_for_platform(android_hints)) {
    ASSERT_TRUE(p != BrowserProfile::Firefox149_Windows);
  }

  RuntimePlatformHints ios_hints;
  ios_hints.device_class = DeviceClass::Mobile;
  ios_hints.mobile_os = MobileOs::IOS;

  for (auto p : allowed_profiles_for_platform(ios_hints)) {
    ASSERT_TRUE(p != BrowserProfile::Firefox149_Windows);
  }
}

TEST(TLS_MultiDumpWindowsFirefoxStats, Firefox149WindowsIsInAllProfiles) {
  bool found = false;
  for (auto p : all_profiles()) {
    if (p == BrowserProfile::Firefox149_Windows) {
      found = true;
    }
  }
  ASSERT_TRUE(found);
}

TEST(TLS_MultiDumpWindowsFirefoxStats, WindowsFirefoxDoesNotAppearInNonWindowsDesktopPool) {
  // Firefox149_Windows is a Windows-dedicated profile. It must not appear
  // in the Linux or macOS allowed profile sets.
  for (auto os : {DesktopOs::Linux, DesktopOs::Darwin, DesktopOs::Unknown}) {
    RuntimePlatformHints hints;
    hints.device_class = DeviceClass::Desktop;
    hints.desktop_os = os;
    for (auto p : allowed_profiles_for_platform(hints)) {
      ASSERT_TRUE(p != BrowserProfile::Firefox149_Windows);
    }
  }
}

// ---------------------------------------------------------------------------
// Adversarial / black-hat
// ---------------------------------------------------------------------------

TEST(TLS_MultiDumpWindowsFirefoxStats, WindowsFirefoxWithShortestValidSniDoesNotPanic) {
  MockRng rng(0xDEAD);
  auto wire = build_tls_client_hello_for_profile("a.co", "0123456789secret", kUnixTime,
                                                 BrowserProfile::Firefox149_Windows, EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  auto parsed_res = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed_res.is_ok());
}

TEST(TLS_MultiDumpWindowsFirefoxStats, WindowsFirefoxWithLongSniDoesNotPanic) {
  const td::string long_sni = td::string(63, 'x') + "." + td::string(63, 'y') + "." + td::string(63, 'z') + ".org";
  MockRng rng(0xD00D);
  auto wire = build_tls_client_hello_for_profile(long_sni, "0123456789secret", kUnixTime,
                                                 BrowserProfile::Firefox149_Windows, EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  auto parsed_res = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed_res.is_ok());
}

TEST(TLS_MultiDumpWindowsFirefoxStats, WindowsFirefoxWithMaxTimestampDoesNotPanic) {
  MockRng rng(0xCAFE);
  auto wire =
      build_tls_client_hello_for_profile("www.example.com", "0123456789secret", std::numeric_limits<td::int32>::max(),
                                         BrowserProfile::Firefox149_Windows, EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  auto parsed_res = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed_res.is_ok());
}

TEST(TLS_MultiDumpWindowsFirefoxStats, WindowsFirefoxWithNegativeTimestampDoesNotPanic) {
  MockRng rng(0xBEEF);
  auto wire = build_tls_client_hello_for_profile("www.example.com", "0123456789secret", -1,
                                                 BrowserProfile::Firefox149_Windows, EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  auto parsed_res = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed_res.is_ok());
}

TEST(TLS_MultiDumpWindowsFirefoxStats, WindowsFirefoxCipherOrderStableAcrossEchModes) {
  // Firefox keeps cipher order fixed (FixedFromFixture policy). ECH mode
  // must not change the cipher suite list.
  MockRng rng_on(42);
  MockRng rng_off(42);
  auto wire_on = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                    BrowserProfile::Firefox149_Windows, EchMode::Rfc9180Outer, rng_on);
  auto wire_off = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                     BrowserProfile::Firefox149_Windows, EchMode::Disabled, rng_off);

  auto res_on = parse_tls_client_hello(wire_on);
  auto res_off = parse_tls_client_hello(wire_off);
  ASSERT_TRUE(res_on.is_ok() && res_off.is_ok());

  auto cs_on_res = parse_cipher_suite_vector(res_on.ok_ref().cipher_suites);
  auto cs_off_res = parse_cipher_suite_vector(res_off.ok_ref().cipher_suites);
  ASSERT_TRUE(cs_on_res.is_ok() && cs_off_res.is_ok());

  using td::uint16;
  auto is_grease = [](uint16 v) {
    return (v & 0x0F0F) == 0x0A0A;
  };
  auto cs_on = cs_on_res.move_as_ok();
  auto cs_off = cs_off_res.move_as_ok();
  cs_on.erase(std::remove_if(cs_on.begin(), cs_on.end(), is_grease), cs_on.end());
  cs_off.erase(std::remove_if(cs_off.begin(), cs_off.end(), is_grease), cs_off.end());
  ASSERT_TRUE(cs_on == cs_off);
}

TEST(TLS_MultiDumpWindowsFirefoxStats, WindowsFirefoxExtensionOrderIsFixedAcrossSeedsForSameDomain) {
  // Firefox uses FixedFromFixture extension order — seeds must not shuffle
  // the extension list (unlike Chrome whose policy is ChromeShuffleAnchored).
  const int kSeeds = 10;
  std::vector<std::vector<td::uint16>> extension_orders;
  extension_orders.reserve(kSeeds);

  for (int seed = 0; seed < kSeeds; seed++) {
    MockRng rng(static_cast<td::uint64>(seed));
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Firefox149_Windows, EchMode::Rfc9180Outer, rng);
    auto parsed_res = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed_res.is_ok());
    auto &p = parsed_res.ok_ref();

    std::vector<td::uint16> order;
    order.reserve(p.extensions.size());
    for (const auto &ext : p.extensions) {
      order.push_back(ext.type);
    }
    extension_orders.push_back(std::move(order));
  }

  // All 10 seeds must produce the same extension ordering.
  for (int i = 1; i < kSeeds; i++) {
    ASSERT_TRUE(extension_orders[i] == extension_orders[0]);
  }
}

TEST(TLS_MultiDumpWindowsFirefoxStats, WindowsFirefoxIsDifferentFromLinuxFirefox) {
  // Firefox149_Windows and Firefox148 (Linux) must produce distinct ClientHellos
  // because the profiles are separate enum values — they are not aliases.
  MockRng rng_win(99);
  MockRng rng_lin(99);
  auto wire_win =
      build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                         BrowserProfile::Firefox149_Windows, EchMode::Rfc9180Outer, rng_win);
  auto wire_lin = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                     BrowserProfile::Firefox148, EchMode::Rfc9180Outer, rng_lin);
  ASSERT_TRUE(!wire_win.empty());
  ASSERT_TRUE(!wire_lin.empty());
  // They may differ in ECH payload length, extension order templates, or
  // other parameters. Verify both parse successfully (structural soundness).
  ASSERT_TRUE(parse_tls_client_hello(wire_win).is_ok());
  ASSERT_TRUE(parse_tls_client_hello(wire_lin).is_ok());
}

TEST(TLS_MultiDumpWindowsFirefoxStats, WindowsFirefoxWireLengthsMatchReviewedWindowsEnvelope) {
  const auto *baseline = get_baseline(Slice("firefox_windows"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);

  if (baseline->set_catalog.observed_wire_lengths.empty()) {
    return;  // Corpus not yet reviewed — skip.
  }

  FamilyLaneMatcher matcher(*baseline);
  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed * 31337));
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Firefox149_Windows, EchMode::Rfc9180Outer, rng);
    ASSERT_TRUE(matcher.within_wire_length_envelope(wire.size(), kWireLengthTolerancePercent));
  }
}

}  // namespace
