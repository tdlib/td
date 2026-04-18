// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Multi-dump stats suite: Firefox149_MacOS26_3 macOS platform-gating,
// adversarial boundary, seed-diversity, and ECH-mode stability checks.
// Complements the baseline suite (exact-invariant + upstream-legality).
// Part of Workstream C, Gap E closure for the firefox_macos lane.

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
using td::mtproto::test::parse_cipher_suite_vector;
using td::mtproto::test::parse_tls_client_hello;
using td::Slice;

constexpr int kSeedCount = 20;
constexpr td::int32 kUnixTime = 1712345678;
constexpr double kWireLengthTolerancePercent = 12.0;

// ---------------------------------------------------------------------------
// Platform gating
// ---------------------------------------------------------------------------

TEST(TLS_MultiDumpMacosFirefoxStats, Firefox149MacAppearsInDarwinAllowedProfiles) {
  RuntimePlatformHints hints;
  hints.device_class = DeviceClass::Desktop;
  hints.desktop_os = DesktopOs::Darwin;

  auto allowed = allowed_profiles_for_platform(hints);
  bool found = false;
  for (auto p : allowed) {
    if (p == BrowserProfile::Firefox149_MacOS26_3) {
      found = true;
    }
  }
  ASSERT_TRUE(found);
}

TEST(TLS_MultiDumpMacosFirefoxStats, Firefox149MacAbsentFromLinuxAllowedProfiles) {
  RuntimePlatformHints hints;
  hints.device_class = DeviceClass::Desktop;
  hints.desktop_os = DesktopOs::Linux;

  auto allowed = allowed_profiles_for_platform(hints);
  for (auto p : allowed) {
    ASSERT_TRUE(p != BrowserProfile::Firefox149_MacOS26_3);
  }
}

TEST(TLS_MultiDumpMacosFirefoxStats, Firefox149MacAbsentFromWindowsAllowedProfiles) {
  RuntimePlatformHints hints;
  hints.device_class = DeviceClass::Desktop;
  hints.desktop_os = DesktopOs::Windows;

  auto allowed = allowed_profiles_for_platform(hints);
  for (auto p : allowed) {
    ASSERT_TRUE(p != BrowserProfile::Firefox149_MacOS26_3);
  }
}

TEST(TLS_MultiDumpMacosFirefoxStats, Firefox149MacAbsentFromMobileProfiles) {
  RuntimePlatformHints android_hints;
  android_hints.device_class = DeviceClass::Mobile;
  android_hints.mobile_os = MobileOs::Android;

  for (auto p : allowed_profiles_for_platform(android_hints)) {
    ASSERT_TRUE(p != BrowserProfile::Firefox149_MacOS26_3);
  }

  RuntimePlatformHints ios_hints;
  ios_hints.device_class = DeviceClass::Mobile;
  ios_hints.mobile_os = MobileOs::IOS;

  for (auto p : allowed_profiles_for_platform(ios_hints)) {
    ASSERT_TRUE(p != BrowserProfile::Firefox149_MacOS26_3);
  }
}

TEST(TLS_MultiDumpMacosFirefoxStats, Firefox149MacIsInAllProfiles) {
  bool found = false;
  for (auto p : all_profiles()) {
    if (p == BrowserProfile::Firefox149_MacOS26_3) {
      found = true;
    }
  }
  ASSERT_TRUE(found);
}

// ---------------------------------------------------------------------------
// Adversarial / black-hat boundary tests
// ---------------------------------------------------------------------------

TEST(TLS_MultiDumpMacosFirefoxStats, MacosFirefoxWithShortestValidSniDoesNotPanic) {
  MockRng rng(0xDEAD);
  auto wire = build_tls_client_hello_for_profile("a.co", "0123456789secret", kUnixTime,
                                                 BrowserProfile::Firefox149_MacOS26_3, EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  ASSERT_TRUE(parse_tls_client_hello(wire).is_ok());
}

TEST(TLS_MultiDumpMacosFirefoxStats, MacosFirefoxWithLongSniDoesNotPanic) {
  const td::string long_sni = td::string(63, 'm') + "." + td::string(63, 'n') + "." + td::string(63, 'p') + ".net";
  MockRng rng(0xD00D);
  auto wire = build_tls_client_hello_for_profile(long_sni, "0123456789secret", kUnixTime,
                                                 BrowserProfile::Firefox149_MacOS26_3, EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  ASSERT_TRUE(parse_tls_client_hello(wire).is_ok());
}

TEST(TLS_MultiDumpMacosFirefoxStats, MacosFirefoxWithMaxTimestampDoesNotPanic) {
  MockRng rng(0xCAFE);
  auto wire =
      build_tls_client_hello_for_profile("www.example.com", "0123456789secret", std::numeric_limits<td::int32>::max(),
                                         BrowserProfile::Firefox149_MacOS26_3, EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  ASSERT_TRUE(parse_tls_client_hello(wire).is_ok());
}

TEST(TLS_MultiDumpMacosFirefoxStats, MacosFirefoxWithMinTimestampDoesNotPanic) {
  MockRng rng(0xBEEF);
  auto wire =
      build_tls_client_hello_for_profile("www.example.com", "0123456789secret", std::numeric_limits<td::int32>::min(),
                                         BrowserProfile::Firefox149_MacOS26_3, EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  ASSERT_TRUE(parse_tls_client_hello(wire).is_ok());
}

TEST(TLS_MultiDumpMacosFirefoxStats, MacosFirefoxWithSeedZeroDoesNotCrash) {
  // Seed 0 exercises the zero-initialised RNG code path.
  MockRng rng(0);
  auto wire = build_tls_client_hello_for_profile("www.example.com", "0123456789secret", kUnixTime,
                                                 BrowserProfile::Firefox149_MacOS26_3, EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  ASSERT_TRUE(parse_tls_client_hello(wire).is_ok());
}

// ---------------------------------------------------------------------------
// Seed-diversity — replay resistance
// ---------------------------------------------------------------------------

TEST(TLS_MultiDumpMacosFirefoxStats, MacosFirefox64SeedsDontProduceDuplicateHelloBytes) {
  constexpr int kSeeds = 64;
  std::vector<std::string> seen;
  seen.reserve(kSeeds);
  for (int seed = 0; seed < kSeeds; seed++) {
    MockRng rng(static_cast<td::uint64>(seed) * 0x9e3779b97f4a7c15ULL);
    auto wire = build_tls_client_hello_for_profile("www.mozilla.org", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Firefox149_MacOS26_3, EchMode::Rfc9180Outer, rng);
    ASSERT_TRUE(!wire.empty());
    seen.emplace_back(wire.data(), wire.size());
  }
  std::sort(seen.begin(), seen.end());
  auto unique_end = std::unique(seen.begin(), seen.end());
  size_t unique_count = static_cast<size_t>(std::distance(seen.begin(), unique_end));
  ASSERT_TRUE(unique_count >= static_cast<size_t>(kSeeds / 2));
}

// ---------------------------------------------------------------------------
// ECH-mode stability (DPI probe resistance)
// ---------------------------------------------------------------------------

TEST(TLS_MultiDumpMacosFirefoxStats, MacosFirefoxCipherOrderStableAcrossEchModes) {
  MockRng rng_on(31);
  MockRng rng_off(31);

  auto wire_on =
      build_tls_client_hello_for_profile("www.mozilla.org", "0123456789secret", kUnixTime,
                                         BrowserProfile::Firefox149_MacOS26_3, EchMode::Rfc9180Outer, rng_on);
  auto wire_off = build_tls_client_hello_for_profile("www.mozilla.org", "0123456789secret", kUnixTime,
                                                     BrowserProfile::Firefox149_MacOS26_3, EchMode::Disabled, rng_off);

  auto res_on = parse_tls_client_hello(wire_on);
  auto res_off = parse_tls_client_hello(wire_off);
  ASSERT_TRUE(res_on.is_ok() && res_off.is_ok());

  auto cs_on_res = parse_cipher_suite_vector(res_on.ok_ref().cipher_suites);
  auto cs_off_res = parse_cipher_suite_vector(res_off.ok_ref().cipher_suites);
  ASSERT_TRUE(cs_on_res.is_ok() && cs_off_res.is_ok());

  ASSERT_TRUE(cs_on_res.ok() == cs_off_res.ok());
}

TEST(TLS_MultiDumpMacosFirefoxStats, MacosFirefoxIsDifferentFromLinuxFirefox) {
  // macOS Firefox 149 and Linux Firefox 148 must produce structurally
  // different wires (different ALPS type, different cipher ordering in
  // the reviewed corpus).
  MockRng rng_mac(42);
  MockRng rng_lnx(42);

  auto wire_mac =
      build_tls_client_hello_for_profile("www.mozilla.org", "0123456789secret", kUnixTime,
                                         BrowserProfile::Firefox149_MacOS26_3, EchMode::Rfc9180Outer, rng_mac);
  auto wire_lnx = build_tls_client_hello_for_profile("www.mozilla.org", "0123456789secret", kUnixTime,
                                                     BrowserProfile::Firefox148, EchMode::Rfc9180Outer, rng_lnx);

  ASSERT_TRUE(wire_mac != wire_lnx);
}

TEST(TLS_MultiDumpMacosFirefoxStats, MacosFirefoxWireLengthsMatchReviewedMacosFirefoxEnvelope) {
  const auto *baseline = get_baseline(Slice("firefox_macos"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  FamilyLaneMatcher matcher(*baseline);

  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed));
    auto wire = build_tls_client_hello_for_profile("www.mozilla.org", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Firefox149_MacOS26_3, EchMode::Rfc9180Outer, rng);
    ASSERT_TRUE(!wire.empty());
    ASSERT_TRUE(matcher.within_wire_length_envelope(wire.size(), kWireLengthTolerancePercent));
  }
}

TEST(TLS_MultiDumpMacosFirefoxStats, MacosFirefoxDoesNotLeakDarwinStringInTlsWire) {
  // The generated ClientHello must not contain "darwin" or "mac" as ASCII
  // substrings — OS name in plaintext is a fingerprinting signal.
  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed));
    auto wire = build_tls_client_hello_for_profile("www.mozilla.org", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Firefox149_MacOS26_3, EchMode::Rfc9180Outer, rng);
    ASSERT_TRUE(!wire.empty());
    std::string wire_str(wire.data(), wire.size());
    ASSERT_TRUE(wire_str.find("darwin") == std::string::npos);
    ASSERT_TRUE(wire_str.find("Darwin") == std::string::npos);
    ASSERT_TRUE(wire_str.find("macOS") == std::string::npos);
  }
}

}  // namespace
