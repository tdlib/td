// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Differential tests against real Chrome 144 and Chrome 146 traffic captures
// from the reviewed Linux desktop corpus:
//   clienthello-chrome144.0.7559.109-ubuntu24.04.pcapng
//   chrome146.0.7680.75-1.pcapng
//   chrome146.0.7680.177-1.pcapng
//   clienthello-tdesktop6.7.3-ubuntu24.04.pcapng  (Chrome TLS engine)
//
// Capture analysis (tshark verified):
//   Cipher suites: GREASE + {1301,1302,1303,C02B,C02F,C02C,C030,CCA9,CCA8,C013,C014,009C,009D,002F,0035}
//   Supported groups: GREASE + {11EC,001D,0017,0018}
//   Key share: GREASE(1-byte) + PQ:11EC(1216 bytes) + x25519(32 bytes)
//   ALPS: 0x44CD (Chrome 133+ family; Chrome 144/146/TDesktop all use this)
//   Extension total: GREASE(pos-0) + 16 shuffled + GREASE(pos-17)  [with ECH]
//   ECH: 0xFE0D; payload in {144,176,208,240}; enc_key=32 bytes X25519
//   compress_certificate: brotli only (alg=0x0002, list_len_byte=2)
//   No 3DES suites; no Firefox-specific extensions (0x0022, 0x001C)
#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedClientHelloReferences.h"
#include "test/stealth/TestHelpers.h"

#include <algorithm>
#include <unordered_set>

namespace {

using namespace td;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;
using namespace td::mtproto::test::fixtures;
using namespace td::mtproto::test::fixtures::reviewed_refs;

std::unordered_set<uint16> as_extension_set(const vector<uint16> &values) {
  return std::unordered_set<uint16>(values.begin(), values.end());
}

std::unordered_set<uint16> as_extension_set_without_type(const vector<uint16> &values, uint16 type_to_remove) {
  std::unordered_set<uint16> result;
  for (auto value : values) {
    if (value != type_to_remove) {
      result.insert(value);
    }
  }
  return result;
}

// Extension type set for ECH-enabled Chrome hello (excludes GREASE and padding 0x0015).
const std::unordered_set<uint16> &chrome_ech_extension_set() {
  static const auto value = as_extension_set(chrome_linux_desktop_ref_non_grease_extensions_without_padding);
  return value;
}

const std::unordered_set<uint16> &chrome_no_ech_extension_set() {
  static const auto value =
      as_extension_set_without_type(chrome_linux_desktop_ref_non_grease_extensions_without_padding, kEchExtensionType);
  return value;
}

void assert_same_extension_set(const std::unordered_set<uint16> &expected, const std::unordered_set<uint16> &observed) {
  ASSERT_EQ(expected.size(), observed.size());
  for (auto type : expected) {
    ASSERT_TRUE(observed.count(type) != 0);
  }
}

string build_chrome133_ech(uint64 seed) {
  MockRng rng(seed);
  return build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, BrowserProfile::Chrome133,
                                            EchMode::Rfc9180Outer, rng);
}

string build_chrome133_no_ech(uint64 seed) {
  MockRng rng(seed);
  return build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, BrowserProfile::Chrome133,
                                            EchMode::Disabled, rng);
}

string build_chrome131_ech(uint64 seed) {
  MockRng rng(seed);
  return build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, BrowserProfile::Chrome131,
                                            EchMode::Rfc9180Outer, rng);
}

// ---------------------------------------------------------------------------
// A.1  Cipher suites
// ---------------------------------------------------------------------------

TEST(ChromeCaptureDifferential, CipherSuiteFirstEntryIsGrease) {
  for (uint64 seed = 0; seed < 20; seed++) {
    auto cs = extract_cipher_suites(build_chrome133_ech(seed));
    ASSERT_FALSE(cs.empty());
    ASSERT_TRUE(is_grease_value(cs[0]));
  }
}

TEST(ChromeCaptureDifferential, CipherSuiteCountIsExactly16) {
  for (uint64 seed = 0; seed < 10; seed++) {
    ASSERT_EQ(16u, extract_cipher_suites(build_chrome133_ech(seed)).size());
  }
}

TEST(ChromeCaptureDifferential, CipherSuiteNonGreaseExactOrderMatchesCapture) {
  for (uint64 seed = 0; seed < 10; seed++) {
    auto cs = extract_cipher_suites(build_chrome133_ech(seed));
    std::vector<uint16> non_grease;
    for (auto c : cs) {
      if (!is_grease_value(c))
        non_grease.push_back(c);
    }
    ASSERT_EQ(chrome_linux_desktop_ref_non_grease_cipher_suites, non_grease);
  }
}

TEST(ChromeCaptureDifferential, CipherSuiteContainsNo3Des) {
  // S9: 3DES removed from modern Chrome builds (iOS 15 / macOS Monterey era).
  for (auto profile : {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Chrome120}) {
    MockRng rng(1);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Rfc9180Outer, rng);
    for (auto c : extract_cipher_suites(wire)) {
      ASSERT_NE(kTlsRsaWith3DesEdeCbcSha, c);
      ASSERT_NE(kTlsEcdheEcdsaWith3DesEdeCbcSha, c);
      ASSERT_NE(kTlsEcdheRsaWith3DesEdeCbcSha, c);
    }
  }
}

TEST(ChromeCaptureDifferential, CipherSuiteGreaseVariesAcrossConnections) {
  // GREASE cipher suite value must vary (not a fixed value like 0xAAAA every time).
  std::unordered_set<uint16> seen;
  for (uint64 seed = 0; seed < 50; seed++) {
    auto cs = extract_cipher_suites(build_chrome133_ech(seed));
    ASSERT_TRUE(is_grease_value(cs[0]));
    seen.insert(cs[0]);
  }
  ASSERT_TRUE(seen.size() >= 2u);
}

TEST(ChromeCaptureDifferential, FirefoxDoesNotShareChromeGreasedCipherOrder) {
  // Firefox cipher suites: no GREASE. First suite is TLS_AES_128_GCM_SHA256 (0x1301).
  MockRng rng(1);
  auto wire_ff = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                    BrowserProfile::Firefox148, EchMode::Rfc9180Outer, rng);
  auto cs_ff = extract_cipher_suites(wire_ff);
  ASSERT_FALSE(cs_ff.empty());
  ASSERT_FALSE(is_grease_value(cs_ff[0]));
  ASSERT_EQ(0x1301u, cs_ff[0]);
}

// ---------------------------------------------------------------------------
// A.2  Supported groups
// ---------------------------------------------------------------------------

TEST(ChromeCaptureDifferential, SupportedGroupsFirstIsGrease) {
  for (uint64 seed = 0; seed < 20; seed++) {
    auto groups = extract_supported_groups(build_chrome133_ech(seed));
    ASSERT_FALSE(groups.empty());
    ASSERT_TRUE(is_grease_value(groups[0]));
  }
}

TEST(ChromeCaptureDifferential, SupportedGroupsCountIs5WithPq) {
  for (uint64 seed = 0; seed < 10; seed++) {
    ASSERT_EQ(5u, extract_supported_groups(build_chrome133_ech(seed)).size());
  }
}

TEST(ChromeCaptureDifferential, SupportedGroupsNonGreaseExactOrderMatchesCapture) {
  for (uint64 seed = 0; seed < 10; seed++) {
    auto groups = extract_supported_groups(build_chrome133_ech(seed));
    std::vector<uint16> non_grease;
    for (auto g : groups) {
      if (!is_grease_value(g))
        non_grease.push_back(g);
    }
    ASSERT_EQ(chrome_linux_desktop_ref_non_grease_supported_groups, non_grease);
  }
}

TEST(ChromeCaptureDifferential, SupportedGroupsNoFfdhe) {
  // FFDHE groups (0x0100, 0x0101) are Firefox-exclusive. Chrome never includes them.
  for (auto profile : {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Chrome120}) {
    MockRng rng(7);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Rfc9180Outer, rng);
    for (auto g : extract_supported_groups(wire)) {
      ASSERT_FALSE(g == 0x0100u || g == 0x0101u);
    }
  }
}

TEST(ChromeCaptureDifferential, SupportedGroupsNoLegacyKyber) {
  // Draft Kyber (0x6399) must never appear in Chrome 133/131/120.
  for (uint64 seed = 0; seed < 10; seed++) {
    for (auto g : extract_supported_groups(build_chrome133_ech(seed))) {
      ASSERT_NE(kPqHybridDraftGroup, g);
    }
  }
}

// ---------------------------------------------------------------------------
// A.3  Key share
// ---------------------------------------------------------------------------

TEST(ChromeCaptureDifferential, KeyShareHasThreeEntries) {
  for (uint64 seed = 0; seed < 10; seed++) {
    auto parsed = parse_tls_client_hello(build_chrome133_ech(seed));
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_EQ(3u, parsed.ok().key_share_entries.size());
  }
}

TEST(ChromeCaptureDifferential, KeyShareFirstEntryIsGreaseWithOneByteKey) {
  for (uint64 seed = 0; seed < 20; seed++) {
    auto parsed = parse_tls_client_hello(build_chrome133_ech(seed));
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_FALSE(parsed.ok().key_share_entries.empty());
    ASSERT_TRUE(is_grease_value(parsed.ok().key_share_entries[0].group));
    ASSERT_EQ(1u, parsed.ok().key_share_entries[0].key_length);
  }
}

TEST(ChromeCaptureDifferential, KeySharePqEntryGroupAndLength) {
  for (uint64 seed = 0; seed < 5; seed++) {
    auto parsed = parse_tls_client_hello(build_chrome133_ech(seed));
    ASSERT_TRUE(parsed.is_ok());
    uint32 pq_found = 0;
    for (const auto &e : parsed.ok().key_share_entries) {
      if (e.group == kPqHybridGroup) {
        pq_found++;
        ASSERT_EQ(kPqHybridKeyShareLength, e.key_length);
      }
    }
    ASSERT_EQ(1u, pq_found);
  }
}

TEST(ChromeCaptureDifferential, KeyShareX25519EntryGroupAndLength) {
  for (uint64 seed = 0; seed < 5; seed++) {
    auto parsed = parse_tls_client_hello(build_chrome133_ech(seed));
    ASSERT_TRUE(parsed.is_ok());
    uint32 x25519_found = 0;
    for (const auto &e : parsed.ok().key_share_entries) {
      if (e.group == kX25519Group) {
        x25519_found++;
        ASSERT_EQ(kX25519KeyShareLength, e.key_length);
      }
    }
    ASSERT_EQ(1u, x25519_found);
  }
}

TEST(ChromeCaptureDifferential, PqGroupInBothSupportedGroupsAndKeyShare) {
  // S3 regression: PQ group must be consistent in both locations.
  for (uint64 seed = 0; seed < 20; seed++) {
    auto parsed = parse_tls_client_hello(build_chrome133_ech(seed));
    ASSERT_TRUE(parsed.is_ok());

    bool in_groups = std::find(parsed.ok().supported_groups.begin(), parsed.ok().supported_groups.end(),
                               kPqHybridGroup) != parsed.ok().supported_groups.end();
    bool in_ks = false;
    for (const auto &e : parsed.ok().key_share_entries) {
      if (e.group == kPqHybridGroup)
        in_ks = true;
    }
    ASSERT_EQ(in_groups, in_ks);
    ASSERT_TRUE(in_groups);
  }
}

// ---------------------------------------------------------------------------
// A.4  Extension set (2 GREASEs + correct non-GREASE set)
// ---------------------------------------------------------------------------

TEST(ChromeCaptureDifferential, EchEnabledExtensionSetMatchesCapture) {
  for (uint64 seed = 0; seed < 10; seed++) {
    auto parsed = parse_tls_client_hello(build_chrome133_ech(seed));
    ASSERT_TRUE(parsed.is_ok());

    std::unordered_set<uint16> observed;
    uint32 grease_count = 0;
    for (const auto &ext : parsed.ok().extensions) {
      if (is_grease_value(ext.type)) {
        grease_count++;
      } else if (ext.type != 0x0015u) {
        observed.insert(ext.type);
      }
    }
    assert_same_extension_set(chrome_ech_extension_set(), observed);
    ASSERT_EQ(2u, grease_count);
  }
}

TEST(ChromeCaptureDifferential, EchDisabledExtensionSetMatchesCapture) {
  for (uint64 seed = 0; seed < 10; seed++) {
    auto parsed = parse_tls_client_hello(build_chrome133_no_ech(seed));
    ASSERT_TRUE(parsed.is_ok());

    std::unordered_set<uint16> observed;
    uint32 grease_count = 0;
    for (const auto &ext : parsed.ok().extensions) {
      if (is_grease_value(ext.type)) {
        grease_count++;
      } else if (ext.type != 0x0015u) {
        observed.insert(ext.type);
      }
    }
    assert_same_extension_set(chrome_no_ech_extension_set(), observed);
    ASSERT_EQ(2u, grease_count);
  }
}

TEST(ChromeCaptureDifferential, TDesktopCaptureCompatibleWithChrome133) {
  // TDesktop 6.7.3 (Chrome TLS engine) captured extension set matches Chrome133.
  auto parsed = parse_tls_client_hello(build_chrome133_ech(42));
  ASSERT_TRUE(parsed.is_ok());

  std::unordered_set<uint16> observed;
  for (const auto &ext : parsed.ok().extensions) {
    if (!is_grease_value(ext.type) && ext.type != 0x0015u)
      observed.insert(ext.type);
  }
  for (auto t : chrome_ech_extension_set()) {
    ASSERT_TRUE(observed.count(t) != 0);
  }
}

// ---------------------------------------------------------------------------
// A.5  ALPS type per profile
// ---------------------------------------------------------------------------

TEST(ChromeCaptureDifferential, Chrome133AlpsIs0x44CD) {
  for (uint64 seed = 0; seed < 10; seed++) {
    auto wire = build_chrome133_ech(seed);
    ASSERT_TRUE(has_extension(wire, 0x44CDu));
    ASSERT_FALSE(has_extension(wire, 0x4469u));
  }
}

TEST(ChromeCaptureDifferential, Chrome131AlpsIs0x4469) {
  for (uint64 seed = 0; seed < 5; seed++) {
    auto wire = build_chrome131_ech(seed);
    ASSERT_TRUE(has_extension(wire, 0x4469u));
    ASSERT_FALSE(has_extension(wire, 0x44CDu));
  }
}

// ---------------------------------------------------------------------------
// A.6  compress_certificate: brotli-only (captured Chrome 144 hex)
// ---------------------------------------------------------------------------

TEST(ChromeCaptureDifferential, CompressCertBrotliOnlyExactBody) {
  // Chrome 144 wire: 00 1b 00 03 02 00 02
  // body = [list_len_byte=0x02] [brotli=0x0002]
  for (uint64 seed = 0; seed < 5; seed++) {
    auto body = extract_extension_body(build_chrome133_ech(seed), 0x001Bu);
    ASSERT_FALSE(body.empty());
    ASSERT_EQ(3u, body.size());
    ASSERT_EQ(static_cast<uint8>(0x02), static_cast<uint8>(body[0]));
    ASSERT_EQ(static_cast<uint8>(0x00), static_cast<uint8>(body[1]));
    ASSERT_EQ(static_cast<uint8>(0x02), static_cast<uint8>(body[2]));
  }
}

TEST(ChromeCaptureDifferential, CompressCertNoZlibNoZstd) {
  for (uint64 seed = 0; seed < 10; seed++) {
    auto body = extract_extension_body(build_chrome133_ech(seed), 0x001Bu);
    ASSERT_FALSE(body.empty());
    const auto *b = reinterpret_cast<const uint8 *>(body.data());
    for (size_t i = 1; i + 1 < body.size(); i += 2) {
      uint16 alg = static_cast<uint16>((static_cast<uint16>(b[i]) << 8) | b[i + 1]);
      ASSERT_NE(0x0001u, alg);  // zlib
      ASSERT_NE(0x0003u, alg);  // zstd
    }
  }
}

// ---------------------------------------------------------------------------
// A.7  No Firefox-specific extensions in Chrome profiles
// ---------------------------------------------------------------------------

TEST(ChromeCaptureDifferential, NoDelegatedCredentialsInChrome) {
  for (auto profile : {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Chrome120}) {
    MockRng rng(1);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Rfc9180Outer, rng);
    ASSERT_FALSE(has_extension(wire, 0x0022u));
  }
}

TEST(ChromeCaptureDifferential, NoRecordSizeLimitInChrome) {
  for (auto profile : {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Chrome120}) {
    MockRng rng(1);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Rfc9180Outer, rng);
    ASSERT_FALSE(has_extension(wire, 0x001Cu));
  }
}

// ---------------------------------------------------------------------------
// A.8  ECH wire: type = 0xFE0D, enc_key declared == actual = 32 bytes
// ---------------------------------------------------------------------------

TEST(ChromeCaptureDifferential, EchTypeIsFe0dNeverFe02) {
  for (uint64 seed = 0; seed < 10; seed++) {
    auto wire = build_chrome133_ech(seed);
    ASSERT_TRUE(has_extension(wire, 0xFE0Du));
    ASSERT_FALSE(has_extension(wire, 0xFE02u));
  }
}

TEST(ChromeCaptureDifferential, EchEncKeyDeclaredEqualsActual32) {
  // S8: declared enc length must match the actual bytes written.
  for (uint64 seed = 0; seed < 20; seed++) {
    auto parsed = parse_tls_client_hello(build_chrome133_ech(seed));
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_EQ(parsed.ok().ech_declared_enc_length, parsed.ok().ech_actual_enc_length);
    ASSERT_EQ(32u, parsed.ok().ech_declared_enc_length);
  }
}

TEST(ChromeCaptureDifferential, EchPayloadLengthBelongsToAllowedChromiumSet) {
  // Capture-derived Chrome policy: payload length must stay in {144,176,208,240}.
  for (uint64 seed = 0; seed < 64; seed++) {
    auto parsed = parse_tls_client_hello(build_chrome133_ech(seed));
    ASSERT_TRUE(parsed.is_ok());
    auto length = parsed.ok().ech_payload_length;
    ASSERT_TRUE(length == 144u || length == 176u || length == 208u || length == 240u);
  }
}

TEST(ChromeCaptureDifferential, EchPayloadLengthVariesAcrossConnections) {
  // Anti-singleton regression: Chrome lane must not collapse to one process-wide ECH length.
  std::unordered_set<uint16> lengths;
  for (uint64 seed = 0; seed < 128; seed++) {
    auto parsed = parse_tls_client_hello(build_chrome133_ech(seed));
    ASSERT_TRUE(parsed.is_ok());
    lengths.insert(parsed.ok().ech_payload_length);
  }
  ASSERT_TRUE(lengths.size() >= 2u);
}

// ---------------------------------------------------------------------------
// A.9  ALPN body: h2, http/1.1 (exact from Chrome 144 capture)
// ---------------------------------------------------------------------------

TEST(ChromeCaptureDifferential, AlpnBodyMatchesChromeCapture) {
  // Chrome 144 ALPN body: 00 0c 02 68 32 08 68 74 74 70 2f 31 2e 31
  static const uint8 kExpected[] = {
      0x00, 0x0c, 0x02, 0x68, 0x32, 0x08, 0x68, 0x74, 0x74, 0x70, 0x2f, 0x31, 0x2e, 0x31,
  };
  for (uint64 seed = 0; seed < 5; seed++) {
    auto body = extract_extension_body(build_chrome133_ech(seed), 0x0010u);
    ASSERT_EQ(Slice(kExpected, sizeof(kExpected)), body);
  }
}

// ---------------------------------------------------------------------------
// A.10  Chrome 120 non-PQ specifics
// ---------------------------------------------------------------------------

TEST(ChromeCaptureDifferential, Chrome120NoPqGroupAnywhere) {
  for (uint64 seed = 0; seed < 10; seed++) {
    MockRng rng(seed);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                   BrowserProfile::Chrome120, EchMode::Rfc9180Outer, rng);
    auto supported_groups = extract_supported_groups(wire);
    auto key_share_groups = extract_key_share_groups(wire);
    std::unordered_set<uint16> gs(supported_groups.begin(), supported_groups.end());
    std::unordered_set<uint16> ks(key_share_groups.begin(), key_share_groups.end());
    ASSERT_EQ(0u, gs.count(kPqHybridGroup));
    ASSERT_EQ(0u, ks.count(kPqHybridGroup));
    ASSERT_EQ(0u, gs.count(kPqHybridDraftGroup));
    ASSERT_NE(0u, ks.count(kX25519Group));
  }
}

TEST(ChromeCaptureDifferential, Chrome120KeyShareTwoEntries) {
  // Chrome 120 (non-PQ): GREASE + x25519 = 2 entries.
  for (uint64 seed = 0; seed < 10; seed++) {
    MockRng rng(seed);
    auto parsed = parse_tls_client_hello(build_tls_client_hello_for_profile(
        "www.google.com", "0123456789secret", 1712345678, BrowserProfile::Chrome120, EchMode::Rfc9180Outer, rng));
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_EQ(2u, parsed.ok().key_share_entries.size());
  }
}

TEST(ChromeCaptureDifferential, Chrome120CipherSuitesMatchChrome133) {
  for (uint64 seed = 0; seed < 5; seed++) {
    MockRng rng(seed);
    auto cs120 = extract_cipher_suites(build_tls_client_hello_for_profile(
        "www.google.com", "0123456789secret", 1712345678, BrowserProfile::Chrome120, EchMode::Rfc9180Outer, rng));
    std::vector<uint16> ng120;
    for (auto c : cs120) {
      if (!is_grease_value(c))
        ng120.push_back(c);
    }
    ASSERT_EQ(chrome_linux_desktop_ref_non_grease_cipher_suites, ng120);
  }
}

// ---------------------------------------------------------------------------
// A.11  Boundary / adversarial
// ---------------------------------------------------------------------------

TEST(ChromeCaptureDifferential, MinimalDomainParses) {
  MockRng rng(1);
  auto wire = build_tls_client_hello_for_profile("a.b", "0123456789secret", 1712345678, BrowserProfile::Chrome133,
                                                 EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(parse_tls_client_hello(wire).is_ok());
  ASSERT_EQ(16u, extract_cipher_suites(wire).size());
}

TEST(ChromeCaptureDifferential, LongDomainTruncatedSafely) {
  string long_domain(300, 'x');
  long_domain += ".example.com";
  MockRng rng(2);
  auto wire = build_tls_client_hello_for_profile(long_domain, "0123456789secret", 1712345678, BrowserProfile::Chrome133,
                                                 EchMode::Rfc9180Outer, rng);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  auto *sni = find_extension(parsed.ok(), 0x0000u);
  ASSERT_TRUE(sni != nullptr);
  // SNI body: list_len(2)+name_type(1)+name_len(2)+name_bytes ≤ 5+253
  ASSERT_TRUE(sni->value.size() <= static_cast<size_t>(5 + 253));
}

TEST(ChromeCaptureDifferential, DistinctSecretsProduceDifferentHmacField) {
  MockRng rng1(1);
  auto wire1 = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                  BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng1);
  MockRng rng2(1);
  auto wire2 = build_tls_client_hello_for_profile("www.google.com", "9999999999xxxxxx", 1712345678,
                                                  BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng2);
  ASSERT_NE(wire1, wire2);
  ASSERT_TRUE(parse_tls_client_hello(wire1).is_ok());
  ASSERT_TRUE(parse_tls_client_hello(wire2).is_ok());
}

TEST(ChromeCaptureDifferential, WireOutputIsDeterministicForSameSeedAndInputs) {
  MockRng rng1(42);
  auto wire1 = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                  BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng1);
  MockRng rng2(42);
  auto wire2 = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                  BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng2);
  ASSERT_EQ(wire1, wire2);
}

}  // namespace
