// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Adversarial tests: A DPI monitoring total ClientHello sizes across
// connections from the same source could fingerprint synthetic ClientHello
// if the size distribution is too narrow, always identical (S1), or
// follows a non-browser-like pattern.

#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;

NetworkRouteHints non_ru_route() {
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = false;
  return hints;
}

NetworkRouteHints ru_route() {
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = true;
  return hints;
}

NetworkRouteHints unknown_route() {
  NetworkRouteHints hints;
  hints.is_known = false;
  hints.is_ru = false;
  return hints;
}

TEST(TlsTotalLengthDistributionAdversarial, DefaultBuildMustNotProduceFixed517) {
  // S1 regression: the original Telegram ClientHello was always exactly 517 bytes.
  for (td::uint64 seed = 1; seed <= 100; seed++) {
    MockRng rng(seed);
    auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678, non_ru_route(), rng);
    ASSERT_TRUE(wire.size() != 517u);
  }
}

TEST(TlsTotalLengthDistributionAdversarial, EchEnabledLaneMustShowLengthVariety) {
  // ECH payload is sampled from {144, 176, 208, 240}.
  // This should produce at least 4 distinct total lengths.
  std::unordered_set<size_t> lengths;
  for (td::uint64 seed = 1; seed <= 200; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
    lengths.insert(wire.size());
  }
  ASSERT_TRUE(lengths.size() >= 4u);
}

TEST(TlsTotalLengthDistributionAdversarial, EchDisabledLaneMustNotBeFixedLength) {
  // Without ECH, padding entropy should still prevent a single fixed length.
  std::unordered_set<size_t> lengths;
  for (td::uint64 seed = 1; seed <= 200; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    lengths.insert(wire.size());
  }
  // Must have at least 2 distinct lengths (padding entropy provides variety).
  ASSERT_TRUE(lengths.size() >= 2u);
}

TEST(TlsTotalLengthDistributionAdversarial, RuRouteMustDisableEchAndStillVaryLength) {
  std::unordered_set<size_t> lengths;
  for (td::uint64 seed = 1; seed <= 200; seed++) {
    MockRng rng(seed);
    auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678, ru_route(), rng);
    lengths.insert(wire.size());
    // Verify ECH is disabled for RU routes.
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    auto hello = parsed.move_as_ok();
    auto *ech = td::mtproto::test::find_extension(hello, 0xFE0D);
    ASSERT_TRUE(ech == nullptr);
  }
  ASSERT_TRUE(lengths.size() >= 2u);
}

TEST(TlsTotalLengthDistributionAdversarial, UnknownRouteMustDisableEch) {
  for (td::uint64 seed = 1; seed <= 50; seed++) {
    MockRng rng(seed);
    auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678, unknown_route(), rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    auto hello = parsed.move_as_ok();
    auto *ech = td::mtproto::test::find_extension(hello, 0xFE0D);
    // Fail-closed: unknown route disables ECH.
    ASSERT_TRUE(ech == nullptr);
  }
}

TEST(TlsTotalLengthDistributionAdversarial, LengthsMustNotClusterWithinNarrowBand) {
  // If all lengths cluster within a very narrow band (e.g., ±2 bytes),
  // a DPI could still trivially fingerprint the range.
  td::vector<size_t> lengths;
  for (td::uint64 seed = 1; seed <= 200; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
    lengths.push_back(wire.size());
  }
  std::sort(lengths.begin(), lengths.end());
  auto min_len = lengths.front();
  auto max_len = lengths.back();
  // ECH payload varies by {144,176,208,240} = 96 byte spread in payload.
  // Total length spread should be at least 30 bytes.
  ASSERT_TRUE(max_len - min_len >= 30u);
}

TEST(TlsTotalLengthDistributionAdversarial, AllProfilesMustProduceValidTlsStructure) {
  // Build every profile with both ECH modes and verify structural parse succeeds.
  auto profiles = td::mtproto::stealth::all_profiles();
  for (auto profile : profiles) {
    for (auto ech_mode : {EchMode::Disabled, EchMode::Rfc9180Outer}) {
      MockRng rng(42);
      auto wire =
          build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile, ech_mode, rng);
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());
    }
  }
}

TEST(TlsTotalLengthDistributionAdversarial, Firefox148LengthsMustNotMatchChromeLengths) {
  // Firefox and Chrome should produce different length distributions.
  std::unordered_set<size_t> chrome_lengths;
  std::unordered_set<size_t> firefox_lengths;
  for (td::uint64 seed = 1; seed <= 100; seed++) {
    MockRng rng1(seed);
    auto wire1 = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                    BrowserProfile::Chrome133, EchMode::Disabled, rng1);
    chrome_lengths.insert(wire1.size());

    MockRng rng2(seed);
    auto wire2 = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                    BrowserProfile::Firefox148, EchMode::Disabled, rng2);
    firefox_lengths.insert(wire2.size());
  }
  // Firefox and Chrome should not have exactly the same set of lengths.
  ASSERT_TRUE(chrome_lengths != firefox_lengths);
}

}  // namespace
