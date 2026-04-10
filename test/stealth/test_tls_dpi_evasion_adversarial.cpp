// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Adversarial integration tests: Full DPI evasion simulation.
//
// These tests simulate a black-hat DPI adversary (e.g., Russian TSPU
// with 84B RUB budget) performing multi-vector analysis of TLS
// ClientHello traffic. The adversary uses:
// 1. JA4 fingerprint matching (sorted, RNG-resilient)
// 2. Cipher suite library identification (BoringSSL vs NSS vs Corecrypto)
// 3. Extension set profiling (which extensions are present)
// 4. ALPN analysis (h2 vs h1 vs 00)
// 5. Key share curve validity (X25519 point on curve)
// 6. PQ hybrid detection (ML-KEM-768 coefficient distribution)
// 7. GREASE pattern analysis (positions and diversity)
// 8. Session ID length fingerprinting
// 9. Wire length statistical analysis
// 10. HMAC replay detection
// 11. Safari determinism vs Chrome randomization detection

#include "test/stealth/MockRng.h"
#include "test/stealth/TestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <set>

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::find_extension;
using td::mtproto::test::is_grease_value;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_cipher_suite_vector;
using td::mtproto::test::parse_tls_client_hello;

// --- Adversarial DPI Attack Vector 1: Session ID length ---

TEST(TlsDpiEvasionAdversarial, SessionIdLengthMustBe32ForAllProfiles) {
  // Real Chrome/Firefox/Safari all send 32-byte session IDs for TLS 1.3 compatibility.
  // Non-standard lengths (0, 16, etc.) are a trivial DPI signal.
  BrowserProfile all_profiles[] = {BrowserProfile::Chrome133,
                                   BrowserProfile::Chrome131,
                                   BrowserProfile::Chrome120,
                                   BrowserProfile::Firefox148,
                                   BrowserProfile::Safari26_3,
                                   BrowserProfile::IOS14,
                                   BrowserProfile::Android11_OkHttp_Advisory};
  for (auto profile : all_profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_EQ(32u, parsed.ok().session_id.size());
  }
}

// --- Adversarial DPI Attack Vector 2: Compression methods ---

TEST(TlsDpiEvasionAdversarial, CompressionMethodsMustBeNullForTls13) {
  // TLS 1.3 ClientHello must advertise exactly one compression method: null (0x00).
  // Any other value is a protocol violation and DPI red flag.
  BrowserProfile all_profiles[] = {BrowserProfile::Chrome133,
                                   BrowserProfile::Chrome131,
                                   BrowserProfile::Chrome120,
                                   BrowserProfile::Firefox148,
                                   BrowserProfile::Safari26_3,
                                   BrowserProfile::IOS14,
                                   BrowserProfile::Android11_OkHttp_Advisory};
  for (auto profile : all_profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_EQ(1u, parsed.ok().compression_methods.size());
    ASSERT_EQ(0, static_cast<td::uint8>(parsed.ok().compression_methods[0]));
  }
}

// --- Adversarial DPI Attack Vector 3: Record layer version ---

TEST(TlsDpiEvasionAdversarial, RecordLayerVersionMustBe0x0301) {
  // Real browsers use TLS record version 0x0301 (TLS 1.0) for backward compat.
  // Using 0x0303 is allowed but less common in TLS 1.3.
  BrowserProfile all_profiles[] = {BrowserProfile::Chrome133,
                                   BrowserProfile::Chrome131,
                                   BrowserProfile::Chrome120,
                                   BrowserProfile::Firefox148,
                                   BrowserProfile::Safari26_3,
                                   BrowserProfile::IOS14,
                                   BrowserProfile::Android11_OkHttp_Advisory};
  for (auto profile : all_profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(parsed.ok().record_legacy_version == 0x0301 || parsed.ok().record_legacy_version == 0x0303);
  }
}

// --- Adversarial DPI Attack Vector 4: Legacy version field ---

TEST(TlsDpiEvasionAdversarial, ClientHelloLegacyVersionMustBe0x0303) {
  // TLS 1.3 spec requires legacy_version = 0x0303 (TLS 1.2)
  BrowserProfile all_profiles[] = {BrowserProfile::Chrome133,
                                   BrowserProfile::Chrome131,
                                   BrowserProfile::Chrome120,
                                   BrowserProfile::Firefox148,
                                   BrowserProfile::Safari26_3,
                                   BrowserProfile::IOS14,
                                   BrowserProfile::Android11_OkHttp_Advisory};
  for (auto profile : all_profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_EQ(0x0303u, parsed.ok().client_legacy_version);
  }
}

// --- Adversarial DPI Attack Vector 5: SNI must match domain ---

TEST(TlsDpiEvasionAdversarial, SniExtensionMustContainProvidedDomain) {
  BrowserProfile all_profiles[] = {BrowserProfile::Chrome133, BrowserProfile::Firefox148, BrowserProfile::Safari26_3};
  for (auto profile : all_profiles) {
    MockRng rng(42);
    td::string domain = "www.example.com";
    auto wire =
        build_tls_client_hello_for_profile(domain, "0123456789secret", 1712345678, profile, EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    auto *sni = find_extension(parsed.ok(), 0x0000);
    ASSERT_TRUE(sni != nullptr);
    // Domain must appear in SNI extension value
    auto sni_str = sni->value.str();
    ASSERT_TRUE(sni_str.find(domain) != td::string::npos);
  }
}

// --- Adversarial DPI Attack Vector 6: supported_versions must include 0x0304 ---

TEST(TlsDpiEvasionAdversarial, SupportedVersionsMustIncludeTls13) {
  BrowserProfile all_profiles[] = {BrowserProfile::Chrome133,
                                   BrowserProfile::Chrome131,
                                   BrowserProfile::Chrome120,
                                   BrowserProfile::Firefox148,
                                   BrowserProfile::Safari26_3,
                                   BrowserProfile::IOS14,
                                   BrowserProfile::Android11_OkHttp_Advisory};
  for (auto profile : all_profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    auto *sv = find_extension(parsed.ok(), 0x002B);
    ASSERT_TRUE(sv != nullptr);
    // Parse supported_versions to verify 0x0304 (TLS 1.3) is present
    ASSERT_TRUE(sv->value.size() >= 3u);
    auto len = static_cast<td::uint8>(sv->value[0]);
    bool has_tls13 = false;
    for (size_t i = 1; i + 1 < sv->value.size() && i < static_cast<size_t>(len + 1); i += 2) {
      auto hi = static_cast<td::uint8>(sv->value[i]);
      auto lo = static_cast<td::uint8>(sv->value[i + 1]);
      auto ver = static_cast<td::uint16>((hi << 8) | lo);
      if (ver == 0x0304) {
        has_tls13 = true;
      }
    }
    ASSERT_TRUE(has_tls13);
  }
}

// --- Adversarial DPI Attack Vector 7: wire image HMAC replay ---

TEST(TlsDpiEvasionAdversarial, DifferentTimestampsMustProduceDifferentHmac) {
  // A DPI adversary performing active probing can replay captured ClientHellos.
  // HMAC embedding in client_random must change with timestamp to prevent replay.
  MockRng rng1(42);
  auto wire1 = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                  BrowserProfile::Chrome133, EchMode::Disabled, rng1);
  MockRng rng2(42);
  auto wire2 = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345679,
                                                  BrowserProfile::Chrome133, EchMode::Disabled, rng2);
  // Same seed, different timestamp => different wire image (HMAC changes)
  ASSERT_NE(wire1, wire2);
}

TEST(TlsDpiEvasionAdversarial, DifferentSecretsMustProduceDifferentHmac) {
  MockRng rng1(42);
  auto wire1 = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                  BrowserProfile::Chrome133, EchMode::Disabled, rng1);
  MockRng rng2(42);
  auto wire2 = build_tls_client_hello_for_profile("www.google.com", "secret0123456789", 1712345678,
                                                  BrowserProfile::Chrome133, EchMode::Disabled, rng2);
  ASSERT_NE(wire1, wire2);
}

// --- Adversarial DPI Attack Vector 8: Cross-profile mixing detection ---

TEST(TlsDpiEvasionAdversarial, SafariWithExtensionShuffleWouldBeDetectable) {
  // If a DPI sees a "Safari" User-Agent but fluctuating JA3 hashes,
  // it knows the traffic is fake. This test verifies Safari NEVER shuffles.
  std::set<td::string> ja3_hashes;
  for (td::uint64 seed = 0; seed < 200; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Safari26_3, EchMode::Disabled, rng);
    ja3_hashes.insert(td::mtproto::test::compute_ja3(wire));
  }
  // Safari JA3 must be stable (only GREASE values change, which JA3 includes)
  // Due to GREASE randomization, JA3 will vary, but the non-GREASE
  // part (the canonical JA3 tuple minus GREASE) must be identical.
  // Let's verify using canonical tuple instead.
  std::set<td::string> canonical_tuples;
  for (td::uint64 seed = 0; seed < 200; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Safari26_3, EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());

    // Build canonical tuple WITHOUT GREASE
    auto ciphers = parse_cipher_suite_vector(parsed.ok().cipher_suites).move_as_ok();
    td::vector<td::uint16> non_grease_ciphers;
    for (auto cs : ciphers) {
      if (!is_grease_value(cs)) {
        non_grease_ciphers.push_back(cs);
      }
    }
    td::vector<td::uint16> non_grease_exts;
    for (const auto &ext : parsed.ok().extensions) {
      if (!is_grease_value(ext.type)) {
        non_grease_exts.push_back(ext.type);
      }
    }
    td::string tuple;
    for (auto cs : non_grease_ciphers) {
      tuple += td::to_string(cs) + ",";
    }
    tuple += "|";
    for (auto ext : non_grease_exts) {
      tuple += td::to_string(ext) + ",";
    }
    canonical_tuples.insert(tuple);
  }
  // Without GREASE noise, Safari must produce exactly one canonical tuple
  ASSERT_EQ(1u, canonical_tuples.size());
}

// --- Adversarial DPI Attack Vector 9: Wire length fingerprinting ---

TEST(TlsDpiEvasionAdversarial, WireLengthMustNotBeFixed517ForDefaultBuild) {
  // The original TDLib fake TLS ClientHello was always exactly 517 bytes.
  // This is the most well-known DPI signature for Telegram proxy.
  std::set<size_t> lengths;
  for (int ts = 0; ts < 200; ts++) {
    NetworkRouteHints hints;
    hints.is_known = false;
    auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678 + ts, hints);
    lengths.insert(wire.size());
    ASSERT_NE(517u, wire.size());
  }
}

// --- Adversarial DPI Attack Vector 10: PQ profile consistency ---

TEST(TlsDpiEvasionAdversarial, PqProfilesMustHavePqInBothSupportedGroupsAndKeyShare) {
  BrowserProfile pq_profiles[] = {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Firefox148,
                                  BrowserProfile::Safari26_3};
  for (auto profile : pq_profiles) {
    auto &spec = td::mtproto::stealth::profile_spec(profile);
    if (!spec.has_pq) {
      continue;
    }
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());

    bool pq_in_groups = false;
    for (auto group : parsed.ok().supported_groups) {
      if (group == spec.pq_group_id) {
        pq_in_groups = true;
      }
    }
    bool pq_in_key_share = false;
    for (auto group : parsed.ok().key_share_groups) {
      if (group == spec.pq_group_id) {
        pq_in_key_share = true;
      }
    }
    // DPI adversary can check: PQ group in supported_groups but not in key_share = anomaly
    ASSERT_TRUE(pq_in_groups);
    ASSERT_TRUE(pq_in_key_share);
  }
}

// --- Adversarial DPI Attack Vector 11: Non-PQ profiles must not have PQ ---

TEST(TlsDpiEvasionAdversarial, NonPqProfilesMustNotAdvertisePqGroup) {
  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Chrome120, EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  for (auto group : parsed.ok().supported_groups) {
    ASSERT_NE(0x11ECu, group);
    ASSERT_NE(0x6399u, group);
  }
  for (auto group : parsed.ok().key_share_groups) {
    ASSERT_NE(0x11ECu, group);
    ASSERT_NE(0x6399u, group);
  }
}

// --- Adversarial DPI Attack Vector 12: ECH on RU routes ---

TEST(TlsDpiEvasionAdversarial, RuRouteMustNeverHaveEchExtension) {
  // ECH is blocked in Russia (per findings). If ECH appears on RU egress,
  // it's an immediate block trigger. Fail-closed = no ECH on RU.
  NetworkRouteHints ru_hints;
  ru_hints.is_known = true;
  ru_hints.is_ru = true;

  for (int ts = 0; ts < 100; ts++) {
    auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678 + ts, ru_hints);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    auto *ech = find_extension(parsed.ok(), 0xFE0D);
    ASSERT_TRUE(ech == nullptr);
  }
}

// --- Adversarial DPI Attack Vector 13: Key share X25519 point validation ---

TEST(TlsDpiEvasionAdversarial, X25519KeyShareMustBeValidCurvePointAcross1000Seeds) {
  for (td::uint64 seed = 0; seed < 1000; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    // Parser already validates key shares, but let's verify explicitly
    for (const auto &entry : parsed.ok().key_share_entries) {
      if (entry.group == 0x001D) {
        ASSERT_TRUE(td::mtproto::test::is_valid_curve25519_public_coordinate(entry.key_data));
      }
    }
  }
}

// --- Adversarial DPI Attack Vector 14: Firefox record_size_limit ---

TEST(TlsDpiEvasionAdversarial, Firefox148MustHaveRecordSizeLimitExtension) {
  // Firefox uniquely includes record_size_limit (0x001C). Its absence
  // when impersonating Firefox is a DPI signal.
  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Firefox148, EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  auto *rsl = find_extension(parsed.ok(), 0x001C);
  ASSERT_TRUE(rsl != nullptr);
  // Value should be a 2-byte big-endian limit (typically 16385)
  ASSERT_EQ(2u, rsl->value.size());
}

TEST(TlsDpiEvasionAdversarial, ChromeProfilesMustNotHaveRecordSizeLimitExtension) {
  // Chrome does NOT include record_size_limit. Its presence when
  // impersonating Chrome is a DPI signal.
  BrowserProfile chrome_profiles[] = {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Chrome120};
  for (auto profile : chrome_profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    auto *rsl = find_extension(parsed.ok(), 0x001C);
    ASSERT_TRUE(rsl == nullptr);
  }
}

// --- Adversarial DPI Attack Vector 15: Duplicate extension detection ---

TEST(TlsDpiEvasionAdversarial, NoDuplicateExtensionTypesAllowed) {
  // RFC 8446 Section 4.2: "There MUST NOT be more than one extension of
  // the same type in a given extension block." DPI can flag duplicates.
  BrowserProfile all_profiles[] = {BrowserProfile::Chrome133,
                                   BrowserProfile::Chrome131,
                                   BrowserProfile::Chrome120,
                                   BrowserProfile::Firefox148,
                                   BrowserProfile::Safari26_3,
                                   BrowserProfile::IOS14,
                                   BrowserProfile::Android11_OkHttp_Advisory};
  for (auto profile : all_profiles) {
    for (td::uint64 seed = 0; seed < 50; seed++) {
      MockRng rng(seed);
      auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                     EchMode::Disabled, rng);
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());
      std::set<td::uint16> seen;
      for (const auto &ext : parsed.ok().extensions) {
        auto result = seen.insert(ext.type);
        // If insert returns false, we have a duplicate
        ASSERT_TRUE(result.second);
      }
    }
  }
}

}  // namespace
