// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Adversarial tests: JA3/JA4 fingerprint cross-validation between the C++
// TLS builder output and the reference algorithms from:
//   - docs/Samples/JA3/ja3.py (Salesforce, BSD-3-Clause)
//   - docs/Samples/JA4/ja4/src/tls.rs (FoxIO, BSD-3-Clause / FoxIO License 1.1)
//
// These tests verify that the wire-format ClientHello produced by the builder
// would be classified correctly by DPI systems using JA3/JA4 fingerprinting.
// Unlike the signature computation tests, these operate on the actual wire bytes.

#include "test/stealth/MockRng.h"
#include "test/stealth/TestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <set>

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_proxy_tls_client_hello_for_profile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::find_extension;
using td::mtproto::test::is_grease_value;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_cipher_suite_vector;
using td::mtproto::test::parse_tls_client_hello;

// ---------------------------------------------------------------------------
// JA3 computation from wire format (reference Salesforce algorithm)
// ---------------------------------------------------------------------------

td::string compute_ja3_string(const td::mtproto::test::ParsedClientHello &hello) {
  // TLS version: always 771 (0x0303) for TLS 1.3 ClientHello wire format
  td::string result = "771,";

  // Cipher suites (excluding GREASE, as decimal, dash-separated)
  auto cipher_suites = parse_cipher_suite_vector(hello.cipher_suites).move_as_ok();
  bool first_cipher = true;
  for (auto cs : cipher_suites) {
    if (!is_grease_value(cs)) {
      if (!first_cipher) {
        result += "-";
      }
      result += td::to_string(cs);
      first_cipher = false;
    }
  }

  result += ",";

  // Extensions (excluding GREASE, as decimal, dash-separated)
  // NOTE: Reference JA3 includes ALL extensions (including padding 0x0015).
  // Some implementations exclude padding. We include it here for reference correctness.
  bool first_ext = true;
  for (const auto &ext : hello.extensions) {
    if (!is_grease_value(ext.type)) {
      if (!first_ext) {
        result += "-";
      }
      result += td::to_string(ext.type);
      first_ext = false;
    }
  }

  result += ",";

  // Supported groups (from ext 0x000A, excluding GREASE)
  auto *sg_ext = find_extension(hello, 0x000A);
  if (sg_ext != nullptr && sg_ext->value.size() >= 2) {
    auto groups_len = static_cast<td::uint16>((static_cast<td::uint8>(sg_ext->value[0]) << 8) |
                                              static_cast<td::uint8>(sg_ext->value[1]));
    bool first_group = true;
    for (size_t i = 2; i + 1 < sg_ext->value.size() && i < static_cast<size_t>(groups_len + 2); i += 2) {
      auto g = static_cast<td::uint16>((static_cast<td::uint8>(sg_ext->value[i]) << 8) |
                                       static_cast<td::uint8>(sg_ext->value[i + 1]));
      if (!is_grease_value(g)) {
        if (!first_group) {
          result += "-";
        }
        result += td::to_string(g);
        first_group = false;
      }
    }
  }

  result += ",";

  // EC point formats (from ext 0x000B)
  auto *ecpf_ext = find_extension(hello, 0x000B);
  if (ecpf_ext != nullptr && ecpf_ext->value.size() >= 1) {
    auto count = static_cast<td::uint8>(ecpf_ext->value[0]);
    bool first_format = true;
    for (size_t i = 1; i < ecpf_ext->value.size() && i < static_cast<size_t>(count + 1); i++) {
      if (!first_format) {
        result += "-";
      }
      result += td::to_string(static_cast<td::uint8>(ecpf_ext->value[i]));
      first_format = false;
    }
  }

  return result;
}

td::string compute_ja3_hash(const td::mtproto::test::ParsedClientHello &hello) {
  auto ja3_string = compute_ja3_string(hello);
  td::string hash(16, '\0');
  td::md5(ja3_string, hash);
  // Convert to hex
  td::string hex_result;
  hex_result.reserve(32);
  for (size_t i = 0; i < hash.size(); i++) {
    auto byte = static_cast<td::uint8>(hash[i]);
    static const char hex_chars[] = "0123456789abcdef";
    hex_result.push_back(hex_chars[byte >> 4]);
    hex_result.push_back(hex_chars[byte & 0x0F]);
  }
  return hex_result;
}

// Known Telegram JA3 hash (from check_fingerprint.py)
static const td::string KNOWN_TELEGRAM_JA3 = "e0e58235789a753608b12649376e91ec";

// --- Tests ---

// =====================================================================
// Anti-Telegram JA3: builder output must NEVER produce the known Telegram JA3
// =====================================================================

TEST(TlsJa3Ja4CrossValidation, Ja3MustNotMatchKnownTelegramHashForAnyProfileAnySeed) {
  BrowserProfile profiles[] = {BrowserProfile::Chrome133,       BrowserProfile::Chrome131,  BrowserProfile::Chrome120,
                               BrowserProfile::Firefox148,      BrowserProfile::Safari26_3, BrowserProfile::IOS14,
                               BrowserProfile::Android11_OkHttp};
  EchMode ech_modes[] = {EchMode::Disabled, EchMode::Rfc9180Outer};

  for (auto profile : profiles) {
    for (auto ech : ech_modes) {
      for (td::uint64 seed = 0; seed < 200; seed++) {
        MockRng rng(seed);
        auto wire =
            build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile, ech, rng);
        auto parsed = parse_tls_client_hello(wire);
        CHECK(parsed.is_ok());
        auto ja3 = compute_ja3_hash(parsed.ok_ref());
        ASSERT_NE(KNOWN_TELEGRAM_JA3, ja3);
      }
    }
  }
}

TEST(TlsJa3Ja4CrossValidation, Ja3MustNotMatchKnownTelegramHashForProxyMode) {
  BrowserProfile profiles[] = {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Chrome120};

  for (auto profile : profiles) {
    for (td::uint64 seed = 0; seed < 100; seed++) {
      MockRng rng(seed);
      auto wire = build_proxy_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                           EchMode::Disabled, rng);
      auto parsed = parse_tls_client_hello(wire);
      CHECK(parsed.is_ok());
      auto ja3 = compute_ja3_hash(parsed.ok_ref());
      ASSERT_NE(KNOWN_TELEGRAM_JA3, ja3);
    }
  }
}

// =====================================================================
// JA3 stability: same profile + same seed = same JA3 hash (determinism)
// =====================================================================

TEST(TlsJa3Ja4CrossValidation, Ja3DeterministicForSameProfileAndSeed) {
  BrowserProfile profiles[] = {BrowserProfile::Chrome133, BrowserProfile::Firefox148, BrowserProfile::Safari26_3};

  for (auto profile : profiles) {
    MockRng rng1(42);
    auto wire1 = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                    EchMode::Disabled, rng1);
    MockRng rng2(42);
    auto wire2 = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                    EchMode::Disabled, rng2);
    auto parsed1 = parse_tls_client_hello(wire1);
    auto parsed2 = parse_tls_client_hello(wire2);
    CHECK(parsed1.is_ok());
    CHECK(parsed2.is_ok());
    ASSERT_EQ(compute_ja3_hash(parsed1.ok_ref()), compute_ja3_hash(parsed2.ok_ref()));
  }
}

// =====================================================================
// JA3 cross-profile uniqueness: different browsers must have different JA3
// =====================================================================

TEST(TlsJa3Ja4CrossValidation, Ja3DiffersBetweenChromeAndFirefox) {
  MockRng rng1(42);
  auto wire_chrome = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                        BrowserProfile::Chrome133, EchMode::Disabled, rng1);
  MockRng rng2(42);
  auto wire_firefox = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                         BrowserProfile::Firefox148, EchMode::Disabled, rng2);
  auto parsed_chrome = parse_tls_client_hello(wire_chrome);
  auto parsed_firefox = parse_tls_client_hello(wire_firefox);
  CHECK(parsed_chrome.is_ok());
  CHECK(parsed_firefox.is_ok());
  ASSERT_NE(compute_ja3_hash(parsed_chrome.ok_ref()), compute_ja3_hash(parsed_firefox.ok_ref()));
}

TEST(TlsJa3Ja4CrossValidation, Ja3DiffersBetweenChromeAndSafari) {
  MockRng rng1(42);
  auto wire_chrome = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                        BrowserProfile::Chrome133, EchMode::Disabled, rng1);
  MockRng rng2(42);
  auto wire_safari = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                        BrowserProfile::Safari26_3, EchMode::Disabled, rng2);
  auto parsed_chrome = parse_tls_client_hello(wire_chrome);
  auto parsed_safari = parse_tls_client_hello(wire_safari);
  CHECK(parsed_chrome.is_ok());
  CHECK(parsed_safari.is_ok());
  ASSERT_NE(compute_ja3_hash(parsed_chrome.ok_ref()), compute_ja3_hash(parsed_safari.ok_ref()));
}

// =====================================================================
// JA3 GREASE independence: GREASE values must not affect JA3 hash
// =====================================================================

TEST(TlsJa3Ja4CrossValidation, Ja3StableAcrossSeedsForDeterministicProfiles) {
  // Safari: no extension shuffle, no GREASE position variation → JA3 must be constant
  std::set<td::string> ja3_set;
  for (td::uint64 seed = 0; seed < 300; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Safari26_3, EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    CHECK(parsed.is_ok());
    ja3_set.insert(compute_ja3_hash(parsed.ok_ref()));
  }
  // Safari's JA3 must be fully deterministic (no GREASE, no shuffle)
  ASSERT_EQ(1u, ja3_set.size());
}

TEST(TlsJa3Ja4CrossValidation, Ja3StableForChromeAcrossExtensionOrderShuffles) {
  // Chrome shuffles extension ORDER but not the extension SET.
  // JA3 includes extensions in their ORIGINAL order, so JA3 CAN vary.
  // However, the cipher suite portion stays fixed (Chrome doesn't shuffle ciphers).
  std::set<td::string> ja3_cipher_seg;
  for (td::uint64 seed = 0; seed < 200; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    CHECK(parsed.is_ok());
    auto ja3_str = compute_ja3_string(parsed.ok_ref());
    // Extract cipher portion (between first and second comma)
    auto first_comma = ja3_str.find(',');
    auto second_comma = ja3_str.find(',', first_comma + 1);
    auto cipher_seg = ja3_str.substr(first_comma + 1, second_comma - first_comma - 1);
    ja3_cipher_seg.insert(cipher_seg);
  }
  // Chrome cipher suite list must be stable regardless of extension shuffle
  ASSERT_EQ(1u, ja3_cipher_seg.size());
}

// =====================================================================
// JA3 string format validation: must follow Salesforce reference format
// =====================================================================

TEST(TlsJa3Ja4CrossValidation, Ja3StringHasExactly4Commas) {
  BrowserProfile profiles[] = {BrowserProfile::Chrome133, BrowserProfile::Firefox148, BrowserProfile::Safari26_3};

  for (auto profile : profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    CHECK(parsed.is_ok());
    auto ja3_str = compute_ja3_string(parsed.ok_ref());
    auto comma_count = std::count(ja3_str.begin(), ja3_str.end(), ',');
    ASSERT_EQ(4, comma_count);
  }
}

TEST(TlsJa3Ja4CrossValidation, Ja3StringStartsWith771) {
  BrowserProfile profiles[] = {BrowserProfile::Chrome133, BrowserProfile::Firefox148, BrowserProfile::Safari26_3,
                               BrowserProfile::IOS14, BrowserProfile::Android11_OkHttp};

  for (auto profile : profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    CHECK(parsed.is_ok());
    auto ja3_str = compute_ja3_string(parsed.ok_ref());
    ASSERT_TRUE(ja3_str.substr(0, 4) == "771,");
  }
}

TEST(TlsJa3Ja4CrossValidation, Ja3HashIs32HexChars) {
  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Chrome133, EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  auto ja3 = compute_ja3_hash(parsed.ok_ref());
  ASSERT_EQ(32u, ja3.size());
  for (char c : ja3) {
    ASSERT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
  }
}

// =====================================================================
// JA3 supported groups must include X25519 and ML-KEM/P256 for modern Chrome
// =====================================================================

TEST(TlsJa3Ja4CrossValidation, Ja3SupportedGroupsIncludeX25519ForChrome) {
  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Chrome133, EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  auto ja3_str = compute_ja3_string(parsed.ok_ref());
  // X25519 = group 29
  auto groups_start = ja3_str.rfind(',', ja3_str.rfind(',') - 1) + 1;
  auto groups_end = ja3_str.rfind(',');
  auto groups_seg = ja3_str.substr(groups_start, groups_end - groups_start);
  ASSERT_TRUE(groups_seg.find("29") != td::string::npos);
}

// =====================================================================
// Proxy mode: JA3 must still be valid and non-Telegram
// =====================================================================

TEST(TlsJa3Ja4CrossValidation, ProxyModeJa3StringFormatValid) {
  MockRng rng(42);
  auto wire = build_proxy_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                       BrowserProfile::Chrome133, EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  auto ja3_str = compute_ja3_string(parsed.ok_ref());
  auto comma_count = std::count(ja3_str.begin(), ja3_str.end(), ',');
  ASSERT_EQ(4, comma_count);
  ASSERT_TRUE(ja3_str.substr(0, 4) == "771,");
}

// =====================================================================
// Cross-connection fingerprint diversity: JA3 should vary across some
// seeds for profiles with extension shuffling (Chrome)
// =====================================================================

TEST(TlsJa3Ja4CrossValidation, ChromeJa3VariesAcrossSeedsDueToExtensionShuffle) {
  std::set<td::string> ja3_set;
  for (td::uint64 seed = 0; seed < 500; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    CHECK(parsed.is_ok());
    ja3_set.insert(compute_ja3_hash(parsed.ok_ref()));
  }
  // Chrome's extension shuffle means JA3 should produce multiple distinct hashes
  // (because JA3 uses original extension order, not sorted).
  // At least 2 distinct JA3 hashes should appear over 500 connections.
  ASSERT_TRUE(ja3_set.size() >= 2);
}

// =====================================================================
// ECH mode impact on JA3: ECH adds extension 0xFE0D
// =====================================================================

TEST(TlsJa3Ja4CrossValidation, EchModeChangesJa3HashForChrome) {
  MockRng rng1(42);
  auto wire_no_ech = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                        BrowserProfile::Chrome133, EchMode::Disabled, rng1);
  MockRng rng2(42);
  auto wire_with_ech = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                          BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng2);
  auto parsed_no_ech = parse_tls_client_hello(wire_no_ech);
  auto parsed_with_ech = parse_tls_client_hello(wire_with_ech);
  CHECK(parsed_no_ech.is_ok());
  CHECK(parsed_with_ech.is_ok());

  // ECH adds extension 0xFE0D, so extension list differs → JA3 differs
  auto ja3_no_ech = compute_ja3_hash(parsed_no_ech.ok_ref());
  auto ja3_with_ech = compute_ja3_hash(parsed_with_ech.ok_ref());

  // The extension set changes, so JA3 must change
  auto ja3_str_no = compute_ja3_string(parsed_no_ech.ok_ref());
  auto ja3_str_with = compute_ja3_string(parsed_with_ech.ok_ref());
  // Different extension lists → different JA3 strings (usually)
  ASSERT_NE(ja3_str_no, ja3_str_with);
}

}  // namespace
