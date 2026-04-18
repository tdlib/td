// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Multi-dump stats suite: Firefox148 Linux-desktop platform-gating,
// adversarial boundary, seed-diversity, and ECH-mode stability checks.
// Complements the baseline suite (exact-invariant + upstream-legality).
// Part of Workstream C, Gap E closure for the firefox_linux_desktop lane.

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

TEST(TLS_MultiDumpLinuxFirefoxStats, Firefox148AppearsInLinuxAllowedProfiles) {
  RuntimePlatformHints hints;
  hints.device_class = DeviceClass::Desktop;
  hints.desktop_os = DesktopOs::Linux;

  auto allowed = allowed_profiles_for_platform(hints);
  bool found = false;
  for (auto p : allowed) {
    if (p == BrowserProfile::Firefox148) {
      found = true;
    }
  }
  ASSERT_TRUE(found);
}

TEST(TLS_MultiDumpLinuxFirefoxStats, Firefox148AbsentFromWindowsAllowedProfiles) {
  RuntimePlatformHints hints;
  hints.device_class = DeviceClass::Desktop;
  hints.desktop_os = DesktopOs::Windows;

  auto allowed = allowed_profiles_for_platform(hints);
  for (auto p : allowed) {
    ASSERT_TRUE(p != BrowserProfile::Firefox148);
  }
}

TEST(TLS_MultiDumpLinuxFirefoxStats, Firefox148AbsentFromDarwinAllowedProfiles) {
  RuntimePlatformHints hints;
  hints.device_class = DeviceClass::Desktop;
  hints.desktop_os = DesktopOs::Darwin;

  auto allowed = allowed_profiles_for_platform(hints);
  for (auto p : allowed) {
    ASSERT_TRUE(p != BrowserProfile::Firefox148);
  }
}

TEST(TLS_MultiDumpLinuxFirefoxStats, Firefox148AbsentFromMobileProfiles) {
  RuntimePlatformHints android_hints;
  android_hints.device_class = DeviceClass::Mobile;
  android_hints.mobile_os = MobileOs::Android;

  for (auto p : allowed_profiles_for_platform(android_hints)) {
    ASSERT_TRUE(p != BrowserProfile::Firefox148);
  }

  RuntimePlatformHints ios_hints;
  ios_hints.device_class = DeviceClass::Mobile;
  ios_hints.mobile_os = MobileOs::IOS;

  for (auto p : allowed_profiles_for_platform(ios_hints)) {
    ASSERT_TRUE(p != BrowserProfile::Firefox148);
  }
}

TEST(TLS_MultiDumpLinuxFirefoxStats, Firefox148IsInAllProfiles) {
  bool found = false;
  for (auto p : all_profiles()) {
    if (p == BrowserProfile::Firefox148) {
      found = true;
    }
  }
  ASSERT_TRUE(found);
}

// ---------------------------------------------------------------------------
// Adversarial / black-hat boundary tests
// ---------------------------------------------------------------------------

TEST(TLS_MultiDumpLinuxFirefoxStats, LinuxFirefoxWithShortSniDoesNotPanic) {
  MockRng rng(0xDEAD);
  auto wire = build_tls_client_hello_for_profile("a.co", "0123456789secret", kUnixTime, BrowserProfile::Firefox148,
                                                 EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  ASSERT_TRUE(parse_tls_client_hello(wire).is_ok());
}

TEST(TLS_MultiDumpLinuxFirefoxStats, LinuxFirefoxWithLongSniDoesNotPanic) {
  const td::string long_sni = td::string(63, 'x') + "." + td::string(63, 'y') + "." + td::string(63, 'z') + ".org";
  MockRng rng(0xD00D);
  auto wire = build_tls_client_hello_for_profile(long_sni, "0123456789secret", kUnixTime, BrowserProfile::Firefox148,
                                                 EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  ASSERT_TRUE(parse_tls_client_hello(wire).is_ok());
}

TEST(TLS_MultiDumpLinuxFirefoxStats, LinuxFirefoxWithMaxTimestampDoesNotPanic) {
  MockRng rng(0xCAFE);
  auto wire =
      build_tls_client_hello_for_profile("www.example.com", "0123456789secret", std::numeric_limits<td::int32>::max(),
                                         BrowserProfile::Firefox148, EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  ASSERT_TRUE(parse_tls_client_hello(wire).is_ok());
}

TEST(TLS_MultiDumpLinuxFirefoxStats, LinuxFirefoxWithMinTimestampDoesNotPanic) {
  MockRng rng(0xBEEF);
  auto wire =
      build_tls_client_hello_for_profile("www.example.com", "0123456789secret", std::numeric_limits<td::int32>::min(),
                                         BrowserProfile::Firefox148, EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  ASSERT_TRUE(parse_tls_client_hello(wire).is_ok());
}

TEST(TLS_MultiDumpLinuxFirefoxStats, LinuxFirefoxWithAllZerosRngDoesNotCrash) {
  // Degenerate seed 0: exercises the zero-initialised RNG code path.
  // Firefox profiles have no GREASE, so the result is fully deterministic.
  MockRng rng(0);
  auto wire = build_tls_client_hello_for_profile("www.example.com", "0123456789secret", kUnixTime,
                                                 BrowserProfile::Firefox148, EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  ASSERT_TRUE(parse_tls_client_hello(wire).is_ok());
}

// ---------------------------------------------------------------------------
// Seed-diversity — replay resistance
// ---------------------------------------------------------------------------

TEST(TLS_MultiDumpLinuxFirefoxStats, LinuxFirefox64SeedsDontProduceDuplicateHelloBytes) {
  // Firefox has fewer randomized fields than Chrome, but session IDs and
  // PQ key material still provide uniqueness. Require at least 50% unique.
  constexpr int kSeeds = 64;
  std::vector<std::string> seen;
  seen.reserve(kSeeds);
  for (int seed = 0; seed < kSeeds; seed++) {
    MockRng rng(static_cast<td::uint64>(seed) * 0x9e3779b97f4a7c15ULL);
    auto wire = build_tls_client_hello_for_profile("www.mozilla.org", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Firefox148, EchMode::Rfc9180Outer, rng);
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

TEST(TLS_MultiDumpLinuxFirefoxStats, LinuxFirefoxCipherOrderStableAcrossEchModes) {
  // ECH mode must not change the cipher-suite order: same seed, both modes.
  MockRng rng_on(99);
  MockRng rng_off(99);

  auto wire_on = build_tls_client_hello_for_profile("www.mozilla.org", "0123456789secret", kUnixTime,
                                                    BrowserProfile::Firefox148, EchMode::Rfc9180Outer, rng_on);
  auto wire_off = build_tls_client_hello_for_profile("www.mozilla.org", "0123456789secret", kUnixTime,
                                                     BrowserProfile::Firefox148, EchMode::Disabled, rng_off);

  auto res_on = parse_tls_client_hello(wire_on);
  auto res_off = parse_tls_client_hello(wire_off);
  ASSERT_TRUE(res_on.is_ok() && res_off.is_ok());

  auto cs_on_res = parse_cipher_suite_vector(res_on.ok_ref().cipher_suites);
  auto cs_off_res = parse_cipher_suite_vector(res_off.ok_ref().cipher_suites);
  ASSERT_TRUE(cs_on_res.is_ok() && cs_off_res.is_ok());

  // Firefox has no GREASE in cipher suites; the vectors should be identical.
  ASSERT_TRUE(cs_on_res.ok() == cs_off_res.ok());
}

TEST(TLS_MultiDumpLinuxFirefoxStats, LinuxFirefoxExtensionOrderIsFixedAcrossSeedsForSameDomain) {
  // Firefox extension order is deterministic (no GREASE shuffle).
  // Same domain + same profile → identical non-randomised extension order
  // across any two seeds.
  MockRng rng_a(7);
  MockRng rng_b(1337);

  auto wire_a = build_tls_client_hello_for_profile("www.mozilla.org", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Firefox148, EchMode::Rfc9180Outer, rng_a);
  auto wire_b = build_tls_client_hello_for_profile("www.mozilla.org", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Firefox148, EchMode::Rfc9180Outer, rng_b);

  auto res_a = parse_tls_client_hello(wire_a);
  auto res_b = parse_tls_client_hello(wire_b);
  ASSERT_TRUE(res_a.is_ok() && res_b.is_ok());

  // Collect extension type codes in order from each parse.
  auto ext_types = [](const auto &parsed) {
    std::vector<td::uint16> types;
    for (const auto &ext : parsed.extensions) {
      types.push_back(ext.type);
    }
    return types;
  };
  ASSERT_TRUE(ext_types(res_a.ok_ref()) == ext_types(res_b.ok_ref()));
}

TEST(TLS_MultiDumpLinuxFirefoxStats, LinuxFirefoxIsDifferentFromLinuxChrome) {
  // Firefox and Chrome on Linux must produce structurally distinguishable
  // ClientHellos (different cipher suites or different extension sets).
  MockRng rng_ff(42);
  MockRng rng_ch(42);

  auto wire_ff = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                    BrowserProfile::Firefox148, EchMode::Rfc9180Outer, rng_ff);
  auto wire_ch = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                    BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng_ch);

  // At minimum the raw wire bytes must differ (different TLS stack, different
  // cipher suites, different padding, different key share lengths).
  ASSERT_TRUE(wire_ff != wire_ch);
}

TEST(TLS_MultiDumpLinuxFirefoxStats, LinuxFirefoxWireLengthsMatchReviewedLinuxFirefoxEnvelope) {
  const auto *baseline = get_baseline(Slice("firefox_linux_desktop"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  FamilyLaneMatcher matcher(*baseline);

  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed));
    auto wire = build_tls_client_hello_for_profile("www.mozilla.org", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Firefox148, EchMode::Rfc9180Outer, rng);
    ASSERT_TRUE(!wire.empty());
    ASSERT_TRUE(matcher.within_wire_length_envelope(wire.size(), kWireLengthTolerancePercent));
  }
}

}  // namespace
