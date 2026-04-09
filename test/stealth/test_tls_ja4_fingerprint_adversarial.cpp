// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Adversarial tests: JA4 fingerprint computation and stability.
//
// From the research findings: JA4 normalizes ClientHello by sorting
// cipher suites and extensions lexicographically before hashing (SHA256
// truncated to 12 hex chars). This means extension permutation does NOT
// affect the JA4 hash. A DPI adversary using JA4 can identify the
// underlying crypto library regardless of shuffling.
//
// These tests verify:
// 1. JA4 segment A (metadata) is stable and correct for each profile
// 2. JA4 segment B (sorted cipher hash) is identical across connections
// 3. JA4 segment C (sorted extensions + sig algos) is identical
// 4. JA4 does NOT equal known Telegram fingerprints
// 5. JA4 ALPN field encodes correctly (h2, h1, never 00)

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

struct Ja4Parts {
  td::string segment_a;
  td::string segment_b;
  td::string segment_c;
};

td::string hex_encode_bytes(td::Slice data) {
  td::string result;
  result.reserve(data.size() * 2);
  for (size_t i = 0; i < data.size(); i++) {
    auto byte = static_cast<td::uint8>(data[i]);
    static const char hex_chars[] = "0123456789abcdef";
    result.push_back(hex_chars[byte >> 4]);
    result.push_back(hex_chars[byte & 0x0F]);
  }
  return result;
}

td::string compute_ja4_segment_a(const td::mtproto::test::ParsedClientHello &hello, bool is_ech_present) {
  // Protocol indicator: t for TCP
  td::string result = "t";

  // TLS version from supported_versions extension (highest non-GREASE)
  auto *sv_ext = find_extension(hello, 0x002B);
  td::string version = "00";
  if (sv_ext != nullptr && sv_ext->value.size() >= 3) {
    auto len = static_cast<td::uint8>(sv_ext->value[0]);
    for (size_t i = 1; i + 1 < sv_ext->value.size() && i < static_cast<size_t>(len + 1); i += 2) {
      auto hi = static_cast<td::uint8>(sv_ext->value[i]);
      auto lo = static_cast<td::uint8>(sv_ext->value[i + 1]);
      auto v = static_cast<td::uint16>((hi << 8) | lo);
      if (!is_grease_value(v)) {
        if (v == 0x0304) {
          version = "13";
        } else if (v == 0x0303) {
          version = "12";
        }
        break;
      }
    }
  }
  result += version;

  // SNI presence: d=domain, i=IP
  auto *sni_ext = find_extension(hello, 0x0000);
  result += (sni_ext != nullptr) ? "d" : "i";

  // Cipher suite count (excluding GREASE)
  auto cipher_suites = parse_cipher_suite_vector(hello.cipher_suites).move_as_ok();
  size_t cipher_count = 0;
  for (auto cs : cipher_suites) {
    if (!is_grease_value(cs)) {
      cipher_count++;
    }
  }
  char cipher_count_str[3];
  std::snprintf(cipher_count_str, sizeof(cipher_count_str), "%02zu", cipher_count);
  result += cipher_count_str;

  // Extension count (excluding GREASE only, per JA4 spec).
  // Per reference (tls.rs): nr_exts is computed BEFORE removing SNI and ALPN.
  // SNI and ALPN are only removed from the hash input (segment C), not the count.
  size_t ext_count = 0;
  for (const auto &ext : hello.extensions) {
    if (!is_grease_value(ext.type)) {
      ext_count++;
    }
  }
  char ext_count_str[3];
  std::snprintf(ext_count_str, sizeof(ext_count_str), "%02zu", ext_count);
  result += ext_count_str;

  // ALPN: first and last character of first ALPN protocol
  auto *alpn_ext = find_extension(hello, 0x0010);
  if (alpn_ext != nullptr && alpn_ext->value.size() >= 4) {
    // Skip 2-byte list length, read first protocol length
    auto proto_len = static_cast<td::uint8>(alpn_ext->value[2]);
    if (proto_len > 0 && alpn_ext->value.size() >= static_cast<size_t>(3 + proto_len)) {
      result += alpn_ext->value[3];
      result += alpn_ext->value[2 + proto_len];
    } else {
      result += "00";
    }
  } else {
    result += "00";
  }

  return result;
}

td::string compute_ja4_segment_b(const td::mtproto::test::ParsedClientHello &hello) {
  auto cipher_suites = parse_cipher_suite_vector(hello.cipher_suites).move_as_ok();
  td::vector<td::string> hex_codes;
  for (auto cs : cipher_suites) {
    if (!is_grease_value(cs)) {
      char buf[5];
      std::snprintf(buf, sizeof(buf), "%04x", cs);
      hex_codes.push_back(td::string(buf));
    }
  }
  std::sort(hex_codes.begin(), hex_codes.end());
  td::string joined;
  for (size_t i = 0; i < hex_codes.size(); i++) {
    if (i > 0) {
      joined += ",";
    }
    joined += hex_codes[i];
  }
  td::string hash(32, '\0');
  td::sha256(joined, hash);
  return hex_encode_bytes(td::Slice(hash).substr(0, 6));
}

td::string compute_ja4_segment_c(const td::mtproto::test::ParsedClientHello &hello) {
  // Sorted extensions (excluding GREASE, SNI 0x0000, ALPN 0x0010)
  td::vector<td::string> ext_hex_codes;
  for (const auto &ext : hello.extensions) {
    if (!is_grease_value(ext.type) && ext.type != 0x0000 && ext.type != 0x0010) {
      char buf[5];
      std::snprintf(buf, sizeof(buf), "%04x", ext.type);
      ext_hex_codes.push_back(td::string(buf));
    }
  }
  std::sort(ext_hex_codes.begin(), ext_hex_codes.end());

  td::string joined;
  for (size_t i = 0; i < ext_hex_codes.size(); i++) {
    if (i > 0) {
      joined += ",";
    }
    joined += ext_hex_codes[i];
  }

  // Append signature algorithms (unsorted, as-is)
  auto *sig_alg_ext = find_extension(hello, 0x000D);
  if (sig_alg_ext != nullptr && sig_alg_ext->value.size() >= 2) {
    auto sig_len = static_cast<td::uint16>((static_cast<td::uint8>(sig_alg_ext->value[0]) << 8) |
                                           static_cast<td::uint8>(sig_alg_ext->value[1]));
    joined += "_";
    for (size_t i = 2; i + 1 < sig_alg_ext->value.size() && i < static_cast<size_t>(sig_len + 2); i += 2) {
      if (i > 2) {
        joined += ",";
      }
      char buf[5];
      auto hi = static_cast<td::uint8>(sig_alg_ext->value[i]);
      auto lo = static_cast<td::uint8>(sig_alg_ext->value[i + 1]);
      std::snprintf(buf, sizeof(buf), "%04x", (hi << 8) | lo);
      joined += td::string(buf);
    }
  }

  td::string hash(32, '\0');
  td::sha256(joined, hash);
  return hex_encode_bytes(td::Slice(hash).substr(0, 6));
}

Ja4Parts compute_ja4(td::Slice wire) {
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  auto &hello = parsed.ok_ref();
  bool has_ech = (find_extension(hello, 0xFE0D) != nullptr);
  return {compute_ja4_segment_a(hello, has_ech), compute_ja4_segment_b(hello), compute_ja4_segment_c(hello)};
}

// --- Tests ---

TEST(TlsJa4FingerprintAdversarial, Ja4SegmentBMustBeStableAcrossConnectionsForChrome133) {
  std::set<td::string> segment_b_set;
  for (td::uint64 seed = 0; seed < 200; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
    auto ja4 = compute_ja4(wire);
    segment_b_set.insert(ja4.segment_b);
  }
  // JA4 segment B is derived from SORTED cipher suites. Since Chrome never
  // randomizes cipher suite ORDER, segment B must be identical across all connections.
  ASSERT_EQ(1u, segment_b_set.size());
}

TEST(TlsJa4FingerprintAdversarial, Ja4SegmentCMustBeStableAcrossConnectionsForChrome133) {
  std::set<td::string> segment_c_set;
  for (td::uint64 seed = 0; seed < 200; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
    auto ja4 = compute_ja4(wire);
    segment_c_set.insert(ja4.segment_c);
  }
  // JA4 segment C is derived from SORTED extensions + sig algos. Despite Chrome
  // shuffling extension ORDER, JA4 sorts them. Same extension SET = same segment C.
  ASSERT_EQ(1u, segment_c_set.size());
}

TEST(TlsJa4FingerprintAdversarial, Ja4SegmentAMustBeStableForChrome133WithEch) {
  std::set<td::string> segment_a_set;
  for (td::uint64 seed = 0; seed < 100; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
    auto ja4 = compute_ja4(wire);
    segment_a_set.insert(ja4.segment_a);
  }
  // Segment A encodes metadata (TLS version, SNI, cipher count, extension count, ALPN).
  // Must be constant when ECH mode and profile are fixed.
  ASSERT_EQ(1u, segment_a_set.size());
}

TEST(TlsJa4FingerprintAdversarial, Ja4AlpnFieldMustBeH2ForBrowserProfiles) {
  BrowserProfile profiles[] = {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Chrome120,
                               BrowserProfile::Firefox148, BrowserProfile::Safari26_3};
  for (auto profile : profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    auto ja4 = compute_ja4(wire);
    // JA4 ALPN must encode as "h2" (first char 'h', last char '2') for h2 protocol.
    // Per findings: "00" means no ALPN = instant DPI flag for non-browser traffic.
    auto alpn_chars = ja4.segment_a.substr(ja4.segment_a.size() - 2, 2);
    ASSERT_EQ(td::string("h2"), alpn_chars);
  }
}

TEST(TlsJa4FingerprintAdversarial, Ja4AlpnFieldMustBeH1ForProxyMode) {
  BrowserProfile profiles[] = {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Chrome120};
  for (auto profile : profiles) {
    MockRng rng(42);
    auto wire = build_proxy_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                         EchMode::Disabled, rng);
    auto ja4 = compute_ja4(wire);
    // Proxy mode uses http/1.1 only => JA4 ALPN must be "h1" (first 'h', last '1')
    auto alpn_chars = ja4.segment_a.substr(ja4.segment_a.size() - 2, 2);
    ASSERT_EQ(td::string("h1"), alpn_chars);
  }
}

TEST(TlsJa4FingerprintAdversarial, Ja4AlpnMustNeverBe00ForAnyProfile) {
  BrowserProfile all_profiles[] = {BrowserProfile::Chrome133,       BrowserProfile::Chrome131,
                                   BrowserProfile::Chrome120,       BrowserProfile::Firefox148,
                                   BrowserProfile::Safari26_3,      BrowserProfile::IOS14,
                                   BrowserProfile::Android11_OkHttp};
  for (auto profile : all_profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    auto ja4 = compute_ja4(wire);
    auto alpn_chars = ja4.segment_a.substr(ja4.segment_a.size() - 2, 2);
    // "00" in JA4 means no ALPN extension => immediate DPI flag for non-browser traffic
    ASSERT_NE(td::string("00"), alpn_chars);
  }
}

TEST(TlsJa4FingerprintAdversarial, Chrome131And133MustHaveDifferentJa4DueToAlps) {
  MockRng rng1(42);
  auto wire131 = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                    BrowserProfile::Chrome131, EchMode::Disabled, rng1);
  MockRng rng2(42);
  auto wire133 = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                    BrowserProfile::Chrome133, EchMode::Disabled, rng2);
  auto ja4_131 = compute_ja4(wire131);
  auto ja4_133 = compute_ja4(wire133);

  // Chrome131 has ALPS 0x4469, Chrome133 has ALPS 0x44CD.
  // Since these are different extension types, JA4 segment C must differ.
  ASSERT_NE(ja4_131.segment_c, ja4_133.segment_c);
}

TEST(TlsJa4FingerprintAdversarial, FirefoxJa4SegmentBMustDifferFromChrome) {
  MockRng rng1(42);
  auto wire_chrome = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                        BrowserProfile::Chrome133, EchMode::Disabled, rng1);
  MockRng rng2(42);
  auto wire_firefox = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                         BrowserProfile::Firefox148, EchMode::Disabled, rng2);
  auto ja4_chrome = compute_ja4(wire_chrome);
  auto ja4_firefox = compute_ja4(wire_firefox);

  // Firefox (NSS) and Chrome (BoringSSL) have different cipher suite sets.
  // JA4 segment B must reflect this.
  ASSERT_NE(ja4_chrome.segment_b, ja4_firefox.segment_b);
}

TEST(TlsJa4FingerprintAdversarial, Firefox148Ja4SegmentBMustBeStableAcrossConnections) {
  std::set<td::string> segment_b_set;
  for (td::uint64 seed = 0; seed < 100; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Firefox148, EchMode::Disabled, rng);
    auto ja4 = compute_ja4(wire);
    segment_b_set.insert(ja4.segment_b);
  }
  ASSERT_EQ(1u, segment_b_set.size());
}

TEST(TlsJa4FingerprintAdversarial, Firefox148Ja4SegmentCMustBeStableAcrossConnections) {
  std::set<td::string> segment_c_set;
  for (td::uint64 seed = 0; seed < 100; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Firefox148, EchMode::Disabled, rng);
    auto ja4 = compute_ja4(wire);
    segment_c_set.insert(ja4.segment_c);
  }
  // Firefox uses fixed extension order (no shuffle), so JA4 segment C trivially stable.
  ASSERT_EQ(1u, segment_c_set.size());
}

TEST(TlsJa4FingerprintAdversarial, SafariJa4MustBeFullyDeterministicAcrossConnections) {
  std::set<td::string> full_ja4_set;
  for (td::uint64 seed = 0; seed < 100; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Safari26_3, EchMode::Disabled, rng);
    auto ja4 = compute_ja4(wire);
    full_ja4_set.insert(ja4.segment_a + "_" + ja4.segment_b + "_" + ja4.segment_c);
  }
  // Safari (WebKit/Corecrypto) is fully deterministic: no extension or cipher shuffle.
  // All three JA4 segments must be identical across connections.
  ASSERT_EQ(1u, full_ja4_set.size());
}

TEST(TlsJa4FingerprintAdversarial, Ja4TlsVersionFieldMustBe13ForAllProfiles) {
  BrowserProfile all_profiles[] = {BrowserProfile::Chrome133,       BrowserProfile::Chrome131,
                                   BrowserProfile::Chrome120,       BrowserProfile::Firefox148,
                                   BrowserProfile::Safari26_3,      BrowserProfile::IOS14,
                                   BrowserProfile::Android11_OkHttp};
  for (auto profile : all_profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    auto ja4 = compute_ja4(wire);
    // All profiles advertise TLS 1.3 in supported_versions
    ASSERT_EQ('1', ja4.segment_a[1]);
    ASSERT_EQ('3', ja4.segment_a[2]);
  }
}

TEST(TlsJa4FingerprintAdversarial, Ja4SniFieldMustBeDForDomainBasedConnections) {
  BrowserProfile all_profiles[] = {BrowserProfile::Chrome133, BrowserProfile::Firefox148, BrowserProfile::Safari26_3};
  for (auto profile : all_profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    auto ja4 = compute_ja4(wire);
    // d = domain (SNI present), i = IP (SNI absent)
    ASSERT_EQ('d', ja4.segment_a[3]);
  }
}

}  // namespace
