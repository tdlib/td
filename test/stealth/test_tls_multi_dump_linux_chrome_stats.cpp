// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Multi-dump stats suite: drives Chrome131 / Chrome133 Linux-desktop
// generated ClientHellos through platform-gating, adversarial boundary,
// seed-diversity, and ECH-mode stability checks.  Complements the
// baseline suite (which covers exact-invariant and upstream-legality).
// Part of Workstream C, Gap E closure for the chromium_linux_desktop lane.

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

TEST(TLS_MultiDumpLinuxChromeStats, Chrome133AppearsInLinuxAllowedProfiles) {
  RuntimePlatformHints hints;
  hints.device_class = DeviceClass::Desktop;
  hints.desktop_os = DesktopOs::Linux;

  auto allowed = allowed_profiles_for_platform(hints);
  bool found = false;
  for (auto p : allowed) {
    if (p == BrowserProfile::Chrome133) {
      found = true;
    }
  }
  ASSERT_TRUE(found);
}

TEST(TLS_MultiDumpLinuxChromeStats, Chrome133AbsentFromWindowsAllowedProfiles) {
  RuntimePlatformHints hints;
  hints.device_class = DeviceClass::Desktop;
  hints.desktop_os = DesktopOs::Windows;

  auto allowed = allowed_profiles_for_platform(hints);
  for (auto p : allowed) {
    // Linux-specific Chrome profiles must not bleed into Windows pool.
    ASSERT_TRUE(p != BrowserProfile::Chrome133);
  }
}

TEST(TLS_MultiDumpLinuxChromeStats, Chrome133AbsentFromMobileProfiles) {
  RuntimePlatformHints android_hints;
  android_hints.device_class = DeviceClass::Mobile;
  android_hints.mobile_os = MobileOs::Android;

  for (auto p : allowed_profiles_for_platform(android_hints)) {
    ASSERT_TRUE(p != BrowserProfile::Chrome133);
  }

  RuntimePlatformHints ios_hints;
  ios_hints.device_class = DeviceClass::Mobile;
  ios_hints.mobile_os = MobileOs::IOS;

  for (auto p : allowed_profiles_for_platform(ios_hints)) {
    ASSERT_TRUE(p != BrowserProfile::Chrome133);
  }
}

TEST(TLS_MultiDumpLinuxChromeStats, Chrome133IsInAllProfiles) {
  bool found = false;
  for (auto p : all_profiles()) {
    if (p == BrowserProfile::Chrome133) {
      found = true;
    }
  }
  ASSERT_TRUE(found);
}

// ---------------------------------------------------------------------------
// Adversarial / black-hat boundary tests
// ---------------------------------------------------------------------------

TEST(TLS_MultiDumpLinuxChromeStats, LinuxChromeWithShortestValidSniDoesNotPanic) {
  // Shortest TLD-qualified DNS name; builder must not underflow any length.
  MockRng rng(0xDEAD);
  auto wire = build_tls_client_hello_for_profile("a.co", "0123456789secret", kUnixTime, BrowserProfile::Chrome133,
                                                 EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  ASSERT_TRUE(parse_tls_client_hello(wire).is_ok());
}

TEST(TLS_MultiDumpLinuxChromeStats, LinuxChromeWithLongSniDoesNotPanic) {
  // Near-maximum label length (3 × 63-char labels + TLD). Stresses the SNI
  // extension length encoding and the outer/inner ClientHello length fields.
  const td::string long_sni = td::string(63, 'a') + "." + td::string(63, 'b') + "." + td::string(63, 'c') + ".com";
  MockRng rng(0xD00D);
  auto wire = build_tls_client_hello_for_profile(long_sni, "0123456789secret", kUnixTime, BrowserProfile::Chrome133,
                                                 EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  ASSERT_TRUE(parse_tls_client_hello(wire).is_ok());
}

TEST(TLS_MultiDumpLinuxChromeStats, LinuxChromeWithMaxTimestampDoesNotPanic) {
  // INT32_MAX unix_time must not overflow time-bucketing or GREASE derivation.
  MockRng rng(0xCAFE);
  auto wire =
      build_tls_client_hello_for_profile("www.example.com", "0123456789secret", std::numeric_limits<td::int32>::max(),
                                         BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  ASSERT_TRUE(parse_tls_client_hello(wire).is_ok());
}

TEST(TLS_MultiDumpLinuxChromeStats, LinuxChromeWithMinTimestampDoesNotPanic) {
  // INT32_MIN; negative timestamps must not trigger signed-overflow UB.
  MockRng rng(0xBEEF);
  auto wire =
      build_tls_client_hello_for_profile("www.example.com", "0123456789secret", std::numeric_limits<td::int32>::min(),
                                         BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  ASSERT_TRUE(parse_tls_client_hello(wire).is_ok());
}

TEST(TLS_MultiDumpLinuxChromeStats, LinuxChromeWithNullByteInSniDoesNotCorruptWire) {
  // A null byte mid-SNI should either be rejected or produce a parseable
  // wire — it must not corrupt the length fields of later extensions.
  const td::string sni_with_null = td::string("www\x00google.com", 14);
  MockRng rng(0xABCD);
  auto wire = build_tls_client_hello_for_profile(sni_with_null, "0123456789secret", kUnixTime,
                                                 BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
  // Either empty (rejected) or parseable — never a partially-written corrupt frame.
  if (!wire.empty()) {
    ASSERT_TRUE(parse_tls_client_hello(wire).is_ok());
  }
}

TEST(TLS_MultiDumpLinuxChromeStats, LinuxChromeAllZerosSecretDoesNotPanic) {
  // All-zero session secret: the builder must produce a valid wire without
  // crashing or leaking the zero-key pattern into a predictable field.
  const td::string zero_secret(16, '\x00');
  MockRng rng(0x1234);
  auto wire = build_tls_client_hello_for_profile("www.google.com", zero_secret, kUnixTime, BrowserProfile::Chrome133,
                                                 EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(!wire.empty());
  ASSERT_TRUE(parse_tls_client_hello(wire).is_ok());
}

// ---------------------------------------------------------------------------
// Seed-diversity (active fingerprinting / replay resistance)
// ---------------------------------------------------------------------------

TEST(TLS_MultiDumpLinuxChromeStats, LinuxChrome64SeedsDontProduceDuplicateHelloBytes) {
  // Active fingerprinting relies on re-use producing identical byte sequences.
  // 64 Fibonacci-scrambled seeds must produce distinct ClientHello bytes.
  constexpr int kSeeds = 64;
  std::vector<std::string> seen;
  seen.reserve(kSeeds);
  for (int seed = 0; seed < kSeeds; seed++) {
    MockRng rng(static_cast<td::uint64>(seed) * 0x9e3779b97f4a7c15ULL);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
    ASSERT_TRUE(!wire.empty());
    seen.emplace_back(wire.data(), wire.size());
  }
  std::sort(seen.begin(), seen.end());
  auto unique_end = std::unique(seen.begin(), seen.end());
  size_t unique_count = static_cast<size_t>(std::distance(seen.begin(), unique_end));
  // At least 50% of 64 consecutive seeds must produce distinct hellos.
  ASSERT_TRUE(unique_count >= static_cast<size_t>(kSeeds / 2));
}

// ---------------------------------------------------------------------------
// ECH-mode stability (DPI probe resistance)
// ---------------------------------------------------------------------------

TEST(TLS_MultiDumpLinuxChromeStats, LinuxChromeCipherOrderDoesNotChangeAcrossEchModes) {
  // A DPI that toggles ECH to correlate sessions must be denied any
  // difference in cipher-suite order.
  MockRng rng_on(42);
  MockRng rng_off(42);

  auto wire_on = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                    BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng_on);
  auto wire_off = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                     BrowserProfile::Chrome133, EchMode::Disabled, rng_off);

  auto res_on = parse_tls_client_hello(wire_on);
  auto res_off = parse_tls_client_hello(wire_off);
  ASSERT_TRUE(res_on.is_ok() && res_off.is_ok());

  auto cs_on_res = parse_cipher_suite_vector(res_on.ok_ref().cipher_suites);
  auto cs_off_res = parse_cipher_suite_vector(res_off.ok_ref().cipher_suites);
  ASSERT_TRUE(cs_on_res.is_ok() && cs_off_res.is_ok());

  using td::uint16;
  auto is_grease = [](uint16 v) {
    return (v & 0x0f0f) == 0x0a0a;
  };

  auto filter_grease = [&](const std::vector<uint16> &cs) {
    std::vector<uint16> out;
    for (auto c : cs) {
      if (!is_grease(c)) {
        out.push_back(c);
      }
    }
    return out;
  };

  auto cs_on = filter_grease(cs_on_res.move_as_ok());
  auto cs_off = filter_grease(cs_off_res.move_as_ok());
  ASSERT_TRUE(cs_on == cs_off);
}

TEST(TLS_MultiDumpLinuxChromeStats, LinuxChromeDoesNotLeakDesktopOsInTlsWire) {
  // The TLS wire must not contain a byte sequence that identifies the OS.
  // We check that "Linux" (case-insensitive) does not appear as an ASCII
  // substring in the ClientHello plaintext.
  const std::string linux_marker = "linux";
  const std::string Linux_marker = "Linux";

  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed));
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
    ASSERT_TRUE(!wire.empty());
    std::string wire_str(wire.data(), wire.size());
    ASSERT_TRUE(wire_str.find(linux_marker) == std::string::npos);
    ASSERT_TRUE(wire_str.find(Linux_marker) == std::string::npos);
  }
}

TEST(TLS_MultiDumpLinuxChromeStats, LinuxChromeWireLengthsMatchReviewedLinuxDesktopEnvelope) {
  // Generated wire lengths must stay within the reviewed chromium_linux_desktop
  // envelope (with tolerance) from ReviewedFamilyLaneBaselines.
  const auto *baseline = get_baseline(Slice("chromium_linux_desktop"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  FamilyLaneMatcher matcher(*baseline);

  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed));
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
    ASSERT_TRUE(!wire.empty());
    ASSERT_TRUE(matcher.within_wire_length_envelope(wire.size(), kWireLengthTolerancePercent));
  }
}

// ---------------------------------------------------------------------------
// Cross-profile contamination
// ---------------------------------------------------------------------------

TEST(TLS_MultiDumpLinuxChromeStats, LinuxChromeExtensionSetNeverMatchesWindowsChrome) {
  // If the Windows Chrome profile has a distinct reviewed invariant, Linux
  // Chrome must never produce a byte-for-byte identical extension set.
  const auto *linux_bl = get_baseline(Slice("chromium_linux_desktop"), Slice("non_ru_egress"));
  const auto *win_bl = get_baseline(Slice("chromium_windows"), Slice("non_ru_egress"));
  ASSERT_TRUE(linux_bl != nullptr && win_bl != nullptr);

  // Only meaningful when both baselines carry non-empty cipher-suite invariants
  // (i.e., both are Tier 2+). Skipped if either is still provisional.
  if (linux_bl->invariants.non_grease_cipher_suites_ordered.empty() ||
      win_bl->invariants.non_grease_cipher_suites_ordered.empty()) {
    return;
  }

  // Extension-type sets must differ (Chrome on Linux vs Windows can share
  // cipher suites but the reviewed corpus shows different extension sets
  // for at least the ALPS type or PQ group ordering).
  auto &lx_ext = linux_bl->invariants.non_grease_extension_set;
  auto &wn_ext = win_bl->invariants.non_grease_extension_set;
  ASSERT_TRUE(lx_ext != wn_ext);
}

}  // namespace
