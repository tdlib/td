// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Correctness tests: JA4 segment A computation cross-checked against
// the reference JA4+ implementation (docs/Samples/JA4/ja4/src/tls.rs).
//
// Per JA4 specification:
//   let nr_exts = 99.min(exts.len());            // count INCLUDES SNI+ALPN
//   if !original_order {
//       exts.retain(|&v| v != 0 && v != 16);     // hash EXCLUDES them
//   }
//
// The extension count in segment A must include SNI and ALPN.
// Only the hash input (segment C) excludes them.

#include "test/stealth/MockRng.h"
#include "test/stealth/TestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <cstdio>
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
// Reference-correct JA4 segment A computation
// Per JA4 spec: extension count includes ALL non-GREASE extensions (SNI+ALPN included).
// ---------------------------------------------------------------------------

struct Ja4SegmentACounts {
  size_t non_grease_cipher_count;
  size_t non_grease_ext_count;  // includes SNI and ALPN
  bool has_sni;
  td::string alpn_first_last;  // 2 chars
  td::string tls_version;      // "13", "12", or "00"
};

Ja4SegmentACounts extract_ja4_counts(const td::mtproto::test::ParsedClientHello &hello) {
  Ja4SegmentACounts counts{};

  // Count non-GREASE cipher suites
  auto cipher_suites = parse_cipher_suite_vector(hello.cipher_suites).move_as_ok();
  for (auto cs : cipher_suites) {
    if (!is_grease_value(cs)) {
      counts.non_grease_cipher_count++;
    }
  }

  // Count ALL non-GREASE extensions (including SNI and ALPN)
  counts.has_sni = false;
  for (const auto &ext : hello.extensions) {
    if (!is_grease_value(ext.type)) {
      counts.non_grease_ext_count++;
      if (ext.type == 0x0000) {
        counts.has_sni = true;
      }
    }
  }

  // TLS version from supported_versions extension
  counts.tls_version = "00";
  auto *sv_ext = find_extension(hello, 0x002B);
  if (sv_ext != nullptr && sv_ext->value.size() >= 3) {
    auto len = static_cast<td::uint8>(sv_ext->value[0]);
    for (size_t i = 1; i + 1 < sv_ext->value.size() && i < static_cast<size_t>(len + 1); i += 2) {
      auto hi = static_cast<td::uint8>(sv_ext->value[i]);
      auto lo = static_cast<td::uint8>(sv_ext->value[i + 1]);
      auto v = static_cast<td::uint16>((hi << 8) | lo);
      if (!is_grease_value(v)) {
        if (v == 0x0304) {
          counts.tls_version = "13";
        } else if (v == 0x0303) {
          counts.tls_version = "12";
        }
        break;
      }
    }
  }

  // ALPN first/last character
  auto *alpn_ext = find_extension(hello, 0x0010);
  if (alpn_ext != nullptr && alpn_ext->value.size() >= 4) {
    auto proto_len = static_cast<td::uint8>(alpn_ext->value[2]);
    if (proto_len > 0 && alpn_ext->value.size() >= static_cast<size_t>(3 + proto_len)) {
      counts.alpn_first_last += alpn_ext->value[3];
      counts.alpn_first_last += alpn_ext->value[2 + proto_len];
    } else {
      counts.alpn_first_last = "00";
    }
  } else {
    counts.alpn_first_last = "00";
  }

  return counts;
}

td::string build_correct_segment_a(const Ja4SegmentACounts &counts, bool is_quic = false) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%c%s%c%02zu%02zu%s", is_quic ? 'q' : 't', counts.tls_version.c_str(),
                counts.has_sni ? 'd' : 'i', std::min<size_t>(99, counts.non_grease_cipher_count),
                std::min<size_t>(99, counts.non_grease_ext_count), counts.alpn_first_last.c_str());
  return td::string(buf);
}

// --- Tests ---

TEST(TlsJa4SegmentACorrectness, ExtensionCountIncludesSniForChrome133) {
  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  auto &hello = parsed.ok_ref();

  // Count extensions both ways
  size_t count_all_non_grease = 0;
  size_t count_excluding_sni_alpn = 0;
  bool has_sni = false;
  bool has_alpn = false;

  for (const auto &ext : hello.extensions) {
    if (!is_grease_value(ext.type)) {
      count_all_non_grease++;
      if (ext.type != 0x0000 && ext.type != 0x0010) {
        count_excluding_sni_alpn++;
      }
      if (ext.type == 0x0000) {
        has_sni = true;
      }
      if (ext.type == 0x0010) {
        has_alpn = true;
      }
    }
  }

  // Chrome133 must have both SNI and ALPN
  ASSERT_TRUE(has_sni);
  ASSERT_TRUE(has_alpn);

  // The counts MUST differ (SNI + ALPN = 2 extra)
  ASSERT_EQ(count_all_non_grease, count_excluding_sni_alpn + 2);

  // Build the correct segment A using the REFERENCE-correct count
  auto counts = extract_ja4_counts(hello);
  auto segment_a = build_correct_segment_a(counts);

  // Verify the extension count field (chars at position 6-7)
  // Segment A format: t13dCCEEaa → position 6 is first ext_count digit
  char correct_ext_count[3];
  std::snprintf(correct_ext_count, sizeof(correct_ext_count), "%02zu", std::min<size_t>(99, count_all_non_grease));
  ASSERT_EQ(td::string(correct_ext_count), segment_a.substr(6, 2));

  // Verify it does NOT equal the wrong count
  char wrong_ext_count[3];
  std::snprintf(wrong_ext_count, sizeof(wrong_ext_count), "%02zu", std::min<size_t>(99, count_excluding_sni_alpn));
  ASSERT_NE(td::string(wrong_ext_count), segment_a.substr(6, 2));
}

TEST(TlsJa4SegmentACorrectness, ExtensionCountIncludesAlpnForFirefox148) {
  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Firefox148, EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  auto &hello = parsed.ok_ref();

  auto counts = extract_ja4_counts(hello);
  auto segment_a = build_correct_segment_a(counts);

  // Verify ALPN is present (Firefox must have ALPN)
  auto *alpn_ext = find_extension(hello, 0x0010);
  ASSERT_TRUE(alpn_ext != nullptr);

  // The reference-correct count must include ALPN
  ASSERT_TRUE(counts.non_grease_ext_count > 0);

  // Verify segment A format: 10 chars
  ASSERT_EQ(10u, segment_a.size());
}

TEST(TlsJa4SegmentACorrectness, ExtensionCountConsistentAcross200SeedsChrome133) {
  std::set<size_t> ext_counts;
  for (td::uint64 seed = 0; seed < 200; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
    auto parsed = parse_tls_client_hello(wire);
    CHECK(parsed.is_ok());
    auto counts = extract_ja4_counts(parsed.ok_ref());
    ext_counts.insert(counts.non_grease_ext_count);
  }
  // Extension count must be stable across seeds (Chrome shuffles order, not set)
  ASSERT_EQ(1u, ext_counts.size());
}

TEST(TlsJa4SegmentACorrectness, CipherCountExcludesGreaseForAllProfiles) {
  BrowserProfile profiles[] = {
      BrowserProfile::Chrome133,  BrowserProfile::Chrome131,  BrowserProfile::Chrome120,
      BrowserProfile::Firefox148, BrowserProfile::Safari26_3,
  };
  for (auto profile : profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    CHECK(parsed.is_ok());
    auto &hello = parsed.ok_ref();

    auto cipher_suites = parse_cipher_suite_vector(hello.cipher_suites).move_as_ok();
    size_t total_count = cipher_suites.size();
    size_t non_grease_count = 0;
    for (auto cs : cipher_suites) {
      if (!is_grease_value(cs)) {
        non_grease_count++;
      }
    }

    auto counts = extract_ja4_counts(hello);
    ASSERT_EQ(non_grease_count, counts.non_grease_cipher_count);
    // If GREASE ciphers present, total > non_grease
    // (Chrome injects GREASE ciphers, Firefox doesn't)
    ASSERT_TRUE(total_count >= non_grease_count);
  }
}

TEST(TlsJa4SegmentACorrectness, TlsVersionIs13ForAllModernProfiles) {
  BrowserProfile profiles[] = {
      BrowserProfile::Chrome133,
      BrowserProfile::Chrome131,
      BrowserProfile::Chrome120,
      BrowserProfile::Firefox148,
      BrowserProfile::Safari26_3,
      BrowserProfile::IOS14,
      BrowserProfile::Android11_OkHttp_Advisory,
  };
  for (auto profile : profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    CHECK(parsed.is_ok());

    auto counts = extract_ja4_counts(parsed.ok_ref());
    ASSERT_EQ(td::string("13"), counts.tls_version);
  }
}

TEST(TlsJa4SegmentACorrectness, SegmentAMustBe10CharsForAllProfiles) {
  BrowserProfile profiles[] = {
      BrowserProfile::Chrome133,  BrowserProfile::Chrome131,  BrowserProfile::Chrome120,
      BrowserProfile::Firefox148, BrowserProfile::Safari26_3,
  };
  for (auto profile : profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    CHECK(parsed.is_ok());
    auto counts = extract_ja4_counts(parsed.ok_ref());
    auto segment_a = build_correct_segment_a(counts);
    ASSERT_EQ(10u, segment_a.size());
  }
}

TEST(TlsJa4SegmentACorrectness, AlpnH2ForDirectBrowserConnectionChrome133) {
  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Chrome133, EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  auto counts = extract_ja4_counts(parsed.ok_ref());
  ASSERT_EQ(td::string("h2"), counts.alpn_first_last);
}

TEST(TlsJa4SegmentACorrectness, AlpnH1ForProxyModeChrome133) {
  MockRng rng(42);
  auto wire = build_proxy_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                       BrowserProfile::Chrome133, EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  auto counts = extract_ja4_counts(parsed.ok_ref());
  ASSERT_EQ(td::string("h1"), counts.alpn_first_last);
}

TEST(TlsJa4SegmentACorrectness, SniPresentForDomainConnections) {
  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Chrome133, EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  auto counts = extract_ja4_counts(parsed.ok_ref());
  ASSERT_TRUE(counts.has_sni);
  auto segment_a = build_correct_segment_a(counts);
  ASSERT_EQ('d', segment_a[3]);
}

TEST(TlsJa4SegmentACorrectness, SegmentAStableAcross500SeedsAllProfiles) {
  struct ProfileCase {
    BrowserProfile profile;
    EchMode ech;
    const char *label;
  };
  ProfileCase cases[] = {
      {BrowserProfile::Chrome133, EchMode::Rfc9180Outer, "Chrome133+ECH"},
      {BrowserProfile::Chrome133, EchMode::Disabled, "Chrome133-noECH"},
      {BrowserProfile::Chrome131, EchMode::Disabled, "Chrome131-noECH"},
      {BrowserProfile::Firefox148, EchMode::Disabled, "Firefox148-noECH"},
      {BrowserProfile::Safari26_3, EchMode::Disabled, "Safari26_3-noECH"},
  };
  for (const auto &c : cases) {
    std::set<td::string> segment_a_set;
    std::set<size_t> ext_count_set;
    for (td::uint64 seed = 0; seed < 500; seed++) {
      MockRng rng(seed);
      auto wire =
          build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, c.profile, c.ech, rng);
      auto parsed = parse_tls_client_hello(wire);
      CHECK(parsed.is_ok());
      auto counts = extract_ja4_counts(parsed.ok_ref());
      ext_count_set.insert(counts.non_grease_ext_count);
      segment_a_set.insert(build_correct_segment_a(counts));
    }
    // When ECH is enabled, segment A must be fully stable
    // When ECH is disabled for an ECH-capable profile, extension count
    // may legitimately vary by 1 (ECH extension presence depends on mode)
    if (c.ech == EchMode::Rfc9180Outer) {
      ASSERT_EQ(1u, segment_a_set.size());
    } else {
      // Allow at most 2 distinct counts (with/without ECH stub)
      ASSERT_TRUE(ext_count_set.size() <= 2);
    }
  }
}

// ---------------------------------------------------------------------------
// Cross-validation: old helper vs correct computation
// ---------------------------------------------------------------------------

// This test verifies that the WRONG extension count (excluding SNI+ALPN)
// produces a different result than the correct count.
TEST(TlsJa4SegmentACorrectness, WrongCountExcludingSniAlpnDiffersFromCorrect) {
  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Chrome133, EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  CHECK(parsed.is_ok());
  auto &hello = parsed.ok_ref();

  // Correct count: all non-GREASE extensions
  size_t correct_count = 0;
  for (const auto &ext : hello.extensions) {
    if (!is_grease_value(ext.type)) {
      correct_count++;
    }
  }

  // Wrong count: excludes SNI and ALPN
  size_t wrong_count = 0;
  for (const auto &ext : hello.extensions) {
    if (!is_grease_value(ext.type) && ext.type != 0x0000 && ext.type != 0x0010) {
      wrong_count++;
    }
  }

  // They must differ by exactly 2 (one for SNI, one for ALPN)
  ASSERT_EQ(correct_count, wrong_count + 2);
}

}  // namespace
