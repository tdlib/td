// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Dedicated iOS Chromium runtime-model suite. This is intentionally separate
// from the baseline contract file so we can keep stronger adversarial checks
// isolated and easier to bisect.

#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedFamilyLaneBaselines.h"
#include "test/stealth/TestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <vector>

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
using td::mtproto::test::find_extension;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_cipher_suite_vector;
using td::mtproto::test::parse_tls_client_hello;
using td::Slice;

constexpr int kSeedCount = 32;
constexpr td::int32 kUnixTime = 1712345678;

std::vector<td::uint16> non_grease_cipher_suites(const td::mtproto::test::ParsedClientHello &hello) {
  auto parsed = parse_cipher_suite_vector(hello.cipher_suites);
  if (parsed.is_error()) {
    return {};
  }
  auto cipher_suites = parsed.move_as_ok();
  cipher_suites.erase(std::remove_if(cipher_suites.begin(), cipher_suites.end(),
                                     [](td::uint16 value) { return (value & 0x0F0F) == 0x0A0A; }),
                      cipher_suites.end());
  return cipher_suites;
}

TEST(TLS_MultiDumpIosChromiumStats, DedicatedFamilyBaselineHasMultipleObservedTemplates) {
  const auto *baseline = get_baseline(Slice("ios_chromium"), Slice("non_ru_egress"));
  ASSERT_TRUE(baseline != nullptr);
  ASSERT_TRUE(baseline->sample_count >= 3u);
  ASSERT_TRUE(baseline->set_catalog.observed_extension_order_templates.size() >= 2u);
}

TEST(TLS_MultiDumpIosChromiumStats, StickyIosPlatformPoolIncludesDedicatedChromiumProfileOnlyOnIos) {
  RuntimePlatformHints ios;
  ios.device_class = DeviceClass::Mobile;
  ios.mobile_os = MobileOs::IOS;

  bool saw_ios_chromium = false;
  for (auto p : allowed_profiles_for_platform(ios)) {
    saw_ios_chromium = saw_ios_chromium || (p == BrowserProfile::Chrome147_IOSChromium);
  }
  ASSERT_TRUE(saw_ios_chromium);

  RuntimePlatformHints linux;
  linux.device_class = DeviceClass::Desktop;
  linux.desktop_os = DesktopOs::Linux;
  for (auto p : allowed_profiles_for_platform(linux)) {
    ASSERT_TRUE(p != BrowserProfile::Chrome147_IOSChromium);
  }
}

TEST(TLS_MultiDumpIosChromiumStats, EchOnCarriesChromiumMobileMarkersAcrossSeeds) {
  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(seed));
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Chrome147_IOSChromium, EchMode::Rfc9180Outer, rng);
    auto parsed_res = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed_res.is_ok());
    auto parsed = parsed_res.move_as_ok();

    ASSERT_TRUE(find_extension(parsed, td::mtproto::test::fixtures::kEchExtensionType) != nullptr);
    ASSERT_TRUE(find_extension(parsed, td::mtproto::test::fixtures::kAlpsChrome133Plus) != nullptr);
    ASSERT_TRUE(find_extension(parsed, 0x0029) != nullptr);
    ASSERT_TRUE(parsed.ech_payload_length != 0);
  }
}

TEST(TLS_MultiDumpIosChromiumStats, EchOffFailClosedDropsEchAndPskButKeepsChromiumAlps) {
  for (int seed = 0; seed < kSeedCount; seed++) {
    MockRng rng(static_cast<td::uint64>(1000 + seed));
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                   BrowserProfile::Chrome147_IOSChromium, EchMode::Disabled, rng);
    auto parsed_res = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed_res.is_ok());
    auto parsed = parsed_res.move_as_ok();

    ASSERT_TRUE(find_extension(parsed, td::mtproto::test::fixtures::kEchExtensionType) == nullptr);
    ASSERT_TRUE(find_extension(parsed, 0x0029) == nullptr);
    ASSERT_TRUE(find_extension(parsed, td::mtproto::test::fixtures::kAlpsChrome133Plus) != nullptr);
    ASSERT_TRUE(parsed.ech_payload_length == 0);
  }
}

TEST(TLS_MultiDumpIosChromiumStats, IosChromiumStaysDistinctFromIos14AppleTlsWire) {
  MockRng rng_chromium(42);
  MockRng rng_ios14(42);

  auto chromium_wire =
      build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                         BrowserProfile::Chrome147_IOSChromium, EchMode::Rfc9180Outer, rng_chromium);
  auto ios14_wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", kUnixTime,
                                                       BrowserProfile::IOS14, EchMode::Disabled, rng_ios14);

  auto chromium_res = parse_tls_client_hello(chromium_wire);
  auto ios14_res = parse_tls_client_hello(ios14_wire);
  ASSERT_TRUE(chromium_res.is_ok());
  ASSERT_TRUE(ios14_res.is_ok());

  auto chromium = chromium_res.move_as_ok();
  auto ios14 = ios14_res.move_as_ok();

  ASSERT_TRUE(find_extension(chromium, td::mtproto::test::fixtures::kAlpsChrome133Plus) != nullptr);
  ASSERT_TRUE(find_extension(ios14, td::mtproto::test::fixtures::kAlpsChrome133Plus) == nullptr);

  auto chromium_ciphers = non_grease_cipher_suites(chromium);
  auto ios14_ciphers = non_grease_cipher_suites(ios14);
  ASSERT_FALSE(chromium_ciphers.empty());
  ASSERT_FALSE(ios14_ciphers.empty());
  ASSERT_TRUE(chromium_ciphers != ios14_ciphers);
}

TEST(TLS_MultiDumpIosChromiumStats, LongSniStressParsesAndKeepsCriticalExtensions) {
  const td::string long_sni = td::string(63, 'a') + "." + td::string(63, 'b') + "." + td::string(63, 'c') + ".com";
  MockRng rng(0xDEADBEEF);

  auto wire = build_tls_client_hello_for_profile(long_sni, "0123456789secret", kUnixTime,
                                                 BrowserProfile::Chrome147_IOSChromium, EchMode::Rfc9180Outer, rng);
  auto parsed_res = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed_res.is_ok());

  auto parsed = parsed_res.move_as_ok();
  ASSERT_TRUE(find_extension(parsed, td::mtproto::test::fixtures::kEchExtensionType) != nullptr);
  ASSERT_TRUE(find_extension(parsed, td::mtproto::test::fixtures::kAlpsChrome133Plus) != nullptr);
}

}  // namespace
