// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Adversarial tests: A sophisticated DPI might correlate GREASE values at
// different extension positions within a single ClientHello or across
// multiple connections to fingerprint the GREASE generation algorithm.
// These tests verify that:
// 1. GREASE values at different positions are NOT always identical.
// 2. GREASE pairs within a ClientHello show no deterministic pattern.
// 3. Across many connections, GREASE values are uniformly distributed.

#include "test/stealth/MockRng.h"
#include "test/stealth/TestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <cmath>
#include <unordered_set>

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::is_grease_value;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;

struct GreasePositions {
  td::vector<td::uint16> grease_extension_types;
  td::vector<td::uint16> grease_cipher_suite_values;
};

GreasePositions extract_grease_values(td::Slice wire) {
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  auto hello = parsed.move_as_ok();

  GreasePositions result;
  for (const auto &ext : hello.extensions) {
    if (is_grease_value(ext.type)) {
      result.grease_extension_types.push_back(ext.type);
    }
  }
  auto cipher_suites = td::mtproto::test::parse_cipher_suite_vector(hello.cipher_suites);
  CHECK(cipher_suites.is_ok());
  for (auto cs : cipher_suites.ok()) {
    if (is_grease_value(cs)) {
      result.grease_cipher_suite_values.push_back(cs);
    }
  }
  return result;
}

TEST(TlsGreaseIndependenceAdversarial, GreaseValuesAtDifferentPositionsMustNotAlwaysBeIdentical) {
  // A DPI correlating GREASE values across positions could detect
  // synthetic ClientHello if all positions always use the same value.
  bool found_different = false;
  for (td::uint64 seed = 1; seed <= 200 && !found_different; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    auto grease = extract_grease_values(wire);
    if (grease.grease_extension_types.size() >= 2) {
      for (size_t i = 1; i < grease.grease_extension_types.size(); i++) {
        if (grease.grease_extension_types[i] != grease.grease_extension_types[0]) {
          found_different = true;
          break;
        }
      }
    }
  }
  ASSERT_TRUE(found_different);
}

TEST(TlsGreaseIndependenceAdversarial, GreaseValueDistributionMustCoverMultipleCodepoints) {
  // Chrome GREASE values use the form 0xNANA where N is a nibble.
  // There are 16 possible GREASE values. Over many connections, we should
  // see substantial diversity — not clustering on a few values.
  std::unordered_set<td::uint16> seen_grease;
  for (td::uint64 seed = 1; seed <= 500; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    auto grease = extract_grease_values(wire);
    for (auto v : grease.grease_extension_types) {
      seen_grease.insert(v);
    }
    for (auto v : grease.grease_cipher_suite_values) {
      seen_grease.insert(v);
    }
  }
  // With 500 connections and multiple GREASE positions each, we must see
  // at least 10 of the 16 possible GREASE values.
  ASSERT_TRUE(seen_grease.size() >= 10u);
}

TEST(TlsGreaseIndependenceAdversarial, CipherSuiteGreaseAndExtensionGreaseMustNotAlwaysMatch) {
  // If cipher_suite GREASE and first extension GREASE always match,
  // it reveals algorithm linkage.
  int match_count = 0;
  int total = 0;
  for (td::uint64 seed = 1; seed <= 300; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    auto grease = extract_grease_values(wire);
    if (!grease.grease_cipher_suite_values.empty() && !grease.grease_extension_types.empty()) {
      total++;
      if (grease.grease_cipher_suite_values[0] == grease.grease_extension_types[0]) {
        match_count++;
      }
    }
  }
  // Under independence, match rate should be ~1/16 ≈ 6.25%.
  // Alert if more than 25% of the time they match.
  if (total > 0) {
    double match_rate = static_cast<double>(match_count) / static_cast<double>(total);
    ASSERT_TRUE(match_rate < 0.25);
  }
}

TEST(TlsGreaseIndependenceAdversarial, GreaseValuesMustAllBeValidRfc8701Format) {
  for (td::uint64 seed = 1; seed <= 200; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    auto grease = extract_grease_values(wire);
    for (auto v : grease.grease_extension_types) {
      ASSERT_TRUE(is_grease_value(v));
    }
    for (auto v : grease.grease_cipher_suite_values) {
      ASSERT_TRUE(is_grease_value(v));
    }
  }
}

TEST(TlsGreaseIndependenceAdversarial, ConsecutiveGreaseSlotsMustNotBeIdenticalInSameClientHello) {
  // Chrome's init_grease ensures consecutive GREASE bytes differ.
  // But this test verifies the end-to-end property: consecutive GREASE
  // extension type values in the same ClientHello must not always be the same.
  int same_consecutive_count = 0;
  int total_consecutive_pairs = 0;
  for (td::uint64 seed = 1; seed <= 300; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    auto grease = extract_grease_values(wire);
    for (size_t i = 1; i < grease.grease_extension_types.size(); i++) {
      total_consecutive_pairs++;
      if (grease.grease_extension_types[i] == grease.grease_extension_types[i - 1]) {
        same_consecutive_count++;
      }
    }
  }
  // Consecutive GREASE values in the init_grease buffer are guaranteed
  // different via the XOR step. Since extensions pull from adjacent GREASE
  // slots, they should rarely be the same. Tolerate up to 5% by rounding.
  if (total_consecutive_pairs > 0) {
    double same_rate = static_cast<double>(same_consecutive_count) / static_cast<double>(total_consecutive_pairs);
    ASSERT_TRUE(same_rate < 0.05);
  }
}

}  // namespace
