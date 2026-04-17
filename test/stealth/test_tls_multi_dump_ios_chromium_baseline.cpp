// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Multi-dump baseline suite: drives the dedicated iOS Chromium runtime
// profile against the reviewed ios_chromium FamilyLaneBaseline for 20
// deterministic seeds. The checked-in iOS fixture corpus contains two
// real mobile families: Apple TLS without ECH/ALPS and Chromium-like
// iOS captures with ECH, ALPS 0x44CD, PQ hybrid key share, and a
// trailing pre_shared_key extension on fresh non-RU handshakes.

#include "test/stealth/FamilyLaneMatchers.h"
#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedFamilyLaneBaselines.h"
#include "test/stealth/TestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

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
using td::mtproto::test::find_extension;
using td::mtproto::test::is_grease_value;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;
using td::Slice;

constexpr int kSeedCount = 20;
constexpr td::int32 kUnixTime = 1712345678;
constexpr double kWireLengthTolerancePercent = 12.0;

std::vector<td::uint16> non_grease_extension_order_without_padding(const td::mtproto::test::ParsedClientHello &hello) {
  std::vector<td::uint16> result;
  result.reserve(hello.extensions.size());
  for (const auto &ext : hello.extensions) {
    if (is_grease_value(ext.type) || ext.type == 0x0015) {
      continue;
    }
    result.push_back(ext.type);
  }
  return result;
}

TEST(TLS_MultiDumpIosChromiumBaseline, ReviewedBaselineEntryIsPresent) {
  const auto *baseline = get_baseline(Slice("ios_chromium"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  ASSERT_TRUE(baseline->sample_count > 0u);
  ASSERT_FALSE(baseline->set_catalog.observed_wire_lengths.empty());
}

TEST(TLS_MultiDumpIosChromiumBaseline, IosChromiumEchOnMatchesReviewedBaselineCatalog) {
  const auto *baseline = get_baseline(Slice("ios_chromium"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  FamilyLaneMatcher matcher(*baseline);

  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed));
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Chrome147_IOSChromium, EchMode::Rfc9180Outer, rng);
    auto parsed_res = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed_res.is_ok());
    auto parsed = parsed_res.move_as_ok();
    ASSERT_TRUE(matcher.passes_upstream_rule_legality(parsed));
    ASSERT_TRUE(matcher.within_wire_length_envelope(wire.size(), kWireLengthTolerancePercent));
    ASSERT_TRUE(matcher.covers_observed_extension_order_template(non_grease_extension_order_without_padding(parsed)));
    ASSERT_TRUE(parsed.ech_payload_length != 0);
    ASSERT_TRUE(matcher.covers_observed_ech_payload_length(parsed.ech_payload_length));
  }
}

TEST(TLS_MultiDumpIosChromiumBaseline, IosChromiumFailClosedRouteDropsEchAndPsk) {
  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed + 1000));
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Chrome147_IOSChromium, EchMode::Disabled, rng);
    auto parsed_res = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed_res.is_ok());
    auto parsed = parsed_res.move_as_ok();
    ASSERT_TRUE(find_extension(parsed, td::mtproto::test::fixtures::kEchExtensionType) == nullptr);
    ASSERT_TRUE(find_extension(parsed, 0x0029) == nullptr);
    ASSERT_TRUE(find_extension(parsed, td::mtproto::test::fixtures::kAlpsChrome133Plus) != nullptr);
    ASSERT_TRUE(find_extension(parsed, 0x0015) == nullptr);
  }
}

TEST(TLS_MultiDumpIosChromiumBaseline, IosChromiumAppearsOnlyInIosAllowedProfiles) {
  RuntimePlatformHints ios_hints;
  ios_hints.device_class = DeviceClass::Mobile;
  ios_hints.mobile_os = MobileOs::IOS;

  bool found_on_ios = false;
  for (auto profile : allowed_profiles_for_platform(ios_hints)) {
    found_on_ios = found_on_ios || profile == BrowserProfile::Chrome147_IOSChromium;
  }
  ASSERT_TRUE(found_on_ios);

  RuntimePlatformHints android_hints;
  android_hints.device_class = DeviceClass::Mobile;
  android_hints.mobile_os = MobileOs::Android;
  for (auto profile : allowed_profiles_for_platform(android_hints)) {
    ASSERT_TRUE(profile != BrowserProfile::Chrome147_IOSChromium);
  }

  RuntimePlatformHints desktop_hints;
  desktop_hints.device_class = DeviceClass::Desktop;
  desktop_hints.desktop_os = DesktopOs::Windows;
  for (auto profile : allowed_profiles_for_platform(desktop_hints)) {
    ASSERT_TRUE(profile != BrowserProfile::Chrome147_IOSChromium);
  }
}

}  // namespace
