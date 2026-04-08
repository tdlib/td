// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Light fuzz tests: Exercise the TLS builder with a wide range of
// random seeds, timestamps, domain lengths and profiles to catch
// edge cases that deterministic tests miss.

#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;

TEST(TlsBuilderLightFuzz, ManyRandomSeedsMustAllProduceValidWire) {
  auto profiles = td::mtproto::stealth::all_profiles();
  for (auto profile : profiles) {
    // Test 200 diverse seeds per profile.
    for (td::uint64 seed = 0; seed < 200; seed++) {
      MockRng rng(seed * 7919 + 1);  // Prime stride for diversity
      auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                     EchMode::Disabled, rng);
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());
    }
  }
}

TEST(TlsBuilderLightFuzz, ExtremeTimestampsMustNotCorruptWire) {
  auto timestamps = td::vector<td::int32>{
      0,            // Unix epoch
      1,            // Minimal positive
      -1,           // Just before epoch
      0x7FFFFFFF,   // Max signed int32
      -2147483647,  // Near min signed int32
      1712345678,   // Normal 2024 timestamp
  };

  for (auto ts : timestamps) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", ts, BrowserProfile::Chrome133,
                                                   EchMode::Disabled, rng);
    // The HMAC at offset 11 contains ts, but the structure must still parse.
    ASSERT_TRUE(wire.size() >= 43u);
    ASSERT_EQ(0x16, static_cast<td::uint8>(wire[0]));
    ASSERT_EQ(0x03, static_cast<td::uint8>(wire[1]));
    ASSERT_EQ(0x01, static_cast<td::uint8>(wire[2]));
  }
}

TEST(TlsBuilderLightFuzz, VariousDomainLengthsMustNotCrash) {
  td::vector<td::string> domains = {
      "a",                                    // 1 char
      "ab.cd",                                // Short
      "www.google.com",                       // Normal
      std::string(63, 'a') + ".example.com",  // Long label
      std::string(200, 'x') + ".test.org",    // Very long (will be truncated)
  };

  for (const auto &domain : domains) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile(domain, "0123456789secret", 1712345678, BrowserProfile::Chrome133,
                                                   EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
  }
}

TEST(TlsBuilderLightFuzz, AllProfilesWithEchEnabledMustNotCrash) {
  auto profiles = td::mtproto::stealth::all_profiles();
  for (auto profile : profiles) {
    for (td::uint64 seed = 0; seed < 50; seed++) {
      MockRng rng(seed * 31337 + 7);
      auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                     EchMode::Rfc9180Outer, rng);
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());
    }
  }
}

TEST(TlsBuilderLightFuzz, SecretVariationsMustProduceValidWire) {
  // Different secrets (all 16 bytes) must produce valid wire.
  for (int i = 0; i < 50; i++) {
    td::string secret(16, static_cast<char>(i));
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", secret, 1712345678, BrowserProfile::Chrome133,
                                                   EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
  }
}

TEST(TlsBuilderLightFuzz, HighEntropyStressMustNotExhaustGraceValues) {
  // Rapidly create many ClientHellos to stress the GREASE/RNG path.
  for (td::uint64 seed = 0; seed < 1000; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    // Quick structural check: valid record type and length.
    ASSERT_TRUE(wire.size() >= 5u);
    ASSERT_EQ(0x16, static_cast<td::uint8>(wire[0]));
    auto record_length =
        (static_cast<td::uint16>(static_cast<td::uint8>(wire[3])) << 8) | static_cast<td::uint8>(wire[4]);
    ASSERT_EQ(static_cast<size_t>(record_length), wire.size() - 5u);
  }
}

}  // namespace
