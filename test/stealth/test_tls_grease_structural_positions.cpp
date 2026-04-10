// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Adversarial tests: GREASE value structural positions.
//
// From the research findings and RFC 8701: GREASE values must appear at
// specific structural positions matching real browser behavior. Chrome
// places GREASE at: cipher suites[0], extensions[0] (before permutation
// block), supported_versions[0], supported_groups[0], and final extension
// (post-permutation). Incorrect GREASE placement is trivially detectable
// by a DPI that checks structural positions.

#include "test/stealth/MockRng.h"
#include "test/stealth/TestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::extract_cipher_suites;
using td::mtproto::test::is_grease_value;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;

TEST(TlsGreaseStructuralPositions, ChromeProfilesMustHaveGreaseAsFirstCipherSuite) {
  BrowserProfile chrome_profiles[] = {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Chrome120};
  for (auto profile : chrome_profiles) {
    for (td::uint64 seed = 0; seed < 100; seed++) {
      MockRng rng(seed);
      auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                     EchMode::Disabled, rng);
      auto ciphers = extract_cipher_suites(wire);
      ASSERT_FALSE(ciphers.empty());
      ASSERT_TRUE(is_grease_value(ciphers[0]));
    }
  }
}

TEST(TlsGreaseStructuralPositions, ChromeProfilesMustHaveGreaseAsFirstExtension) {
  BrowserProfile chrome_profiles[] = {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Chrome120};
  for (auto profile : chrome_profiles) {
    for (td::uint64 seed = 0; seed < 100; seed++) {
      MockRng rng(seed);
      auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                     EchMode::Disabled, rng);
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());
      ASSERT_FALSE(parsed.ok().extensions.empty());
      // First extension in Chrome is always a GREASE extension
      ASSERT_TRUE(is_grease_value(parsed.ok().extensions[0].type));
    }
  }
}

TEST(TlsGreaseStructuralPositions, ChromeProfilesMustHaveGreaseInSupportedGroups) {
  BrowserProfile chrome_profiles[] = {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Chrome120};
  for (auto profile : chrome_profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_FALSE(parsed.ok().supported_groups.empty());
    // Chrome puts GREASE at position 0 in supported_groups
    ASSERT_TRUE(is_grease_value(parsed.ok().supported_groups[0]));
    // Only one GREASE value in supported_groups
    size_t grease_count = 0;
    for (auto group : parsed.ok().supported_groups) {
      if (is_grease_value(group)) {
        grease_count++;
      }
    }
    ASSERT_EQ(1u, grease_count);
  }
}

TEST(TlsGreaseStructuralPositions, SafariMustHaveGreaseInCipherSuites) {
  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Safari26_3, EchMode::Disabled, rng);
  auto ciphers = extract_cipher_suites(wire);
  ASSERT_FALSE(ciphers.empty());
  // Safari also uses GREASE in cipher suites
  ASSERT_TRUE(is_grease_value(ciphers[0]));
}

TEST(TlsGreaseStructuralPositions, SafariMustHaveGreaseAsFirstExtension) {
  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Safari26_3, EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  ASSERT_FALSE(parsed.ok().extensions.empty());
  ASSERT_TRUE(is_grease_value(parsed.ok().extensions[0].type));
}

TEST(TlsGreaseStructuralPositions, GreaseValuesInCipherAndExtensionsMustBeIndependent) {
  // If cipher GREASE and extension GREASE always have the same value,
  // it's a fingerprinting signal (real browsers choose independently).
  bool any_differ = false;
  for (td::uint64 seed = 0; seed < 200; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    auto ciphers = extract_cipher_suites(wire);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    if (ciphers[0] != parsed.ok().extensions[0].type) {
      any_differ = true;
      break;
    }
  }
  ASSERT_TRUE(any_differ);
}

TEST(TlsGreaseStructuralPositions, GreaseValuesMustBeValidRfc8701Format) {
  // RFC 8701: GREASE values are 0x?A?A where ? is any nibble
  for (td::uint64 seed = 0; seed < 200; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    auto ciphers = extract_cipher_suites(wire);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());

    // Check all GREASE values in cipher suites
    for (auto cs : ciphers) {
      if (is_grease_value(cs)) {
        auto hi = static_cast<td::uint8>((cs >> 8) & 0xFF);
        auto lo = static_cast<td::uint8>(cs & 0xFF);
        ASSERT_EQ(hi, lo);
        ASSERT_EQ(0x0A, hi & 0x0F);
      }
    }

    // Check all GREASE values in extensions
    for (const auto &ext : parsed.ok().extensions) {
      if (is_grease_value(ext.type)) {
        auto hi = static_cast<td::uint8>((ext.type >> 8) & 0xFF);
        auto lo = static_cast<td::uint8>(ext.type & 0xFF);
        ASSERT_EQ(hi, lo);
        ASSERT_EQ(0x0A, hi & 0x0F);
      }
    }

    // Check all GREASE values in supported_groups
    for (auto group : parsed.ok().supported_groups) {
      if (is_grease_value(group)) {
        auto hi = static_cast<td::uint8>((group >> 8) & 0xFF);
        auto lo = static_cast<td::uint8>(group & 0xFF);
        ASSERT_EQ(hi, lo);
        ASSERT_EQ(0x0A, hi & 0x0F);
      }
    }
  }
}

TEST(TlsGreaseStructuralPositions, GreaseValuesInSupportedVersionsMustBeValid) {
  for (td::uint64 seed = 0; seed < 50; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());

    auto *sv_ext = td::mtproto::test::find_extension(parsed.ok(), 0x002B);
    ASSERT_TRUE(sv_ext != nullptr);
    ASSERT_TRUE(sv_ext->value.size() >= 3u);
    auto len = static_cast<td::uint8>(sv_ext->value[0]);
    ASSERT_TRUE(len >= 6u);
    // First version entry should be GREASE for Chrome profiles
    auto hi = static_cast<td::uint8>(sv_ext->value[1]);
    auto lo = static_cast<td::uint8>(sv_ext->value[2]);
    auto first_version = static_cast<td::uint16>((hi << 8) | lo);
    ASSERT_TRUE(is_grease_value(first_version));
  }
}

TEST(TlsGreaseStructuralPositions, ChromeLastExtensionMustBeGrease) {
  // Chrome has GREASE at the end of extension list (after permutation block)
  for (td::uint64 seed = 0; seed < 100; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    auto &exts = parsed.ok().extensions;
    ASSERT_TRUE(exts.size() >= 2u);
    // Find last non-padding extension
    size_t last_idx = exts.size() - 1;
    // If last is padding (0x0015), look at the one before
    if (exts[last_idx].type == 0x0015) {
      ASSERT_TRUE(last_idx > 0);
      last_idx--;
    }
    ASSERT_TRUE(is_grease_value(exts[last_idx].type));
  }
}

TEST(TlsGreaseStructuralPositions, FixedProfilesMustHaveGreaseInCorrectPositions) {
  // Fixed advisory profiles (iOS14, Android11_OkHttp_Advisory) also use GREASE
  BrowserProfile fixed_profiles[] = {BrowserProfile::IOS14, BrowserProfile::Android11_OkHttp_Advisory};
  for (auto profile : fixed_profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    auto ciphers = extract_cipher_suites(wire);
    ASSERT_FALSE(ciphers.empty());
    ASSERT_TRUE(is_grease_value(ciphers[0]));

    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_FALSE(parsed.ok().extensions.empty());
    ASSERT_TRUE(is_grease_value(parsed.ok().extensions[0].type));
  }
}

}  // namespace
