// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Differential tests against real Firefox 148 and Firefox 149 traffic captures
// from the reviewed Linux desktop corpus:
//   clienthello-firefox148.0.2-ubuntu24.04.pcapng
//   firefox149.0.pcapng
//
// Capture analysis (tshark verified):
//   Cipher suites (no GREASE): {1301,1303,1302,C02B,C02F,CCA9,CCA8,C02C,C030,C00A,C009,C013,C014,009C,009D,002F,0035}
//   Extension order (FIXED, same in FF148 and FF149):
//     SNI(0000) extMasterSecret(0017) renegotiationInfo(FF01) supportedGroups(000A)
//     ecPointFormats(000B) sessionTicket(0023) ALPN(0010) statusRequest(0005)
//     delegatedCredentials(0022) SCT(0012) keyShare(0033) supportedVersions(002B)
//     signatureAlgorithms(000D) pskKeyExchangeModes(002D) recordSizeLimit(001C)
//     compressCertificate(001B) ECH(FE0D)
//   No GREASE anywhere (neither cipher suites nor extensions).
//   ECH is the LAST extension.
//   ECH payload: 239 bytes (fixed, matches real Firefox capture).
//   ECH enc key: X25519 (32 bytes).
//   Supported groups (no GREASE): {11EC, 001D, 0017, 0018, 0019, 0100, 0101} (7 groups, includes FFDHE).
//   key_share: PQ:11EC(1216) + x25519(32) + secp256r1(65) = 3 entries.
//   compress_certificate: zlib+brotli+zstd (alg list = 06 00 01 00 02 00 03).
//   No ALPS extension (Chrome-exclusive).
//   delegated_credentials (0x0022) and record_size_limit (0x001C): Firefox-specific.
#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedClientHelloReferences.h"
#include "test/stealth/TestHelpers.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace {

using namespace td;
using namespace td::mtproto::stealth;
using namespace td::mtproto::test;
using namespace td::mtproto::test::fixtures;
using namespace td::mtproto::test::fixtures::reviewed_refs;

string build_ff148_ech(uint64 seed) {
  MockRng rng(seed);
  return build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                            BrowserProfile::Firefox148, EchMode::Rfc9180Outer, rng);
}

string build_ff148_no_ech(uint64 seed) {
  MockRng rng(seed);
  return build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                            BrowserProfile::Firefox148, EchMode::Disabled, rng);
}

// ---------------------------------------------------------------------------
// B.1  Cipher suites: exact Firefox 148/149 observed order, no GREASE
// ---------------------------------------------------------------------------

TEST(FirefoxCaptureDifferential, CipherSuiteFirstIsNotGrease) {
  // Firefox 148/149: no GREASE. First suite = 0x1301.
  for (uint64 seed = 0; seed < 20; seed++) {
    auto cs = extract_cipher_suites(build_ff148_ech(seed));
    ASSERT_FALSE(cs.empty());
    ASSERT_FALSE(is_grease_value(cs[0]));
    ASSERT_EQ(0x1301u, cs[0]);
  }
}

TEST(FirefoxCaptureDifferential, CipherSuiteCountIsExactly17) {
  // Firefox 148/149: 17 cipher suites (no GREASE slot).
  for (uint64 seed = 0; seed < 10; seed++) {
    ASSERT_EQ(17u, extract_cipher_suites(build_ff148_ech(seed)).size());
  }
}

TEST(FirefoxCaptureDifferential, CipherSuiteExactOrderMatchesCapture) {
  // Both Firefox 148 and 149 captured the same cipher suite order.
  for (uint64 seed = 0; seed < 10; seed++) {
    ASSERT_EQ(firefox_linux_desktop_ref_cipher_suites, extract_cipher_suites(build_ff148_ech(seed)));
  }
}

TEST(FirefoxCaptureDifferential, CipherSuiteNoGreaseEverPresent) {
  for (uint64 seed = 0; seed < 20; seed++) {
    for (auto c : extract_cipher_suites(build_ff148_ech(seed))) {
      ASSERT_FALSE(is_grease_value(c));
    }
  }
}

TEST(FirefoxCaptureDifferential, CipherSuiteContainsNo3Des) {
  MockRng rng(1);
  auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                 BrowserProfile::Firefox148, EchMode::Rfc9180Outer, rng);
  for (auto c : extract_cipher_suites(wire)) {
    ASSERT_NE(kTlsRsaWith3DesEdeCbcSha, c);
    ASSERT_NE(kTlsEcdheEcdsaWith3DesEdeCbcSha, c);
    ASSERT_NE(kTlsEcdheRsaWith3DesEdeCbcSha, c);
  }
}

TEST(FirefoxCaptureDifferential, CipherSuiteOrderIsStableAcrossSeeds) {
  // Firefox uses FixedFromFixture: cipher order must be identical for every seed.
  auto first = extract_cipher_suites(build_ff148_ech(1));
  for (uint64 seed = 2; seed < 30; seed++) {
    ASSERT_EQ(first, extract_cipher_suites(build_ff148_ech(seed)));
  }
}

// ---------------------------------------------------------------------------
// B.2  Extension order: fixed (FixedFromFixture), matches both FF148 and FF149
// ---------------------------------------------------------------------------

TEST(FirefoxCaptureDifferential, ExtensionOrderExactlyMatchesCapture) {
  // Both Firefox 148 and 149 use identical extension order.
  for (uint64 seed = 0; seed < 10; seed++) {
    auto parsed = parse_tls_client_hello(build_ff148_ech(seed));
    ASSERT_TRUE(parsed.is_ok());

    std::vector<uint16> observed;
    for (const auto &ext : parsed.ok().extensions) {
      observed.push_back(ext.type);
    }
    ASSERT_EQ(firefox_linux_desktop_ref_extension_order, observed);
  }
}

TEST(FirefoxCaptureDifferential, ExtensionOrderIsStableAcrossSeeds) {
  // FixedFromFixture: order must be deterministic regardless of RNG seed.
  std::vector<uint16> first;
  {
    auto parsed = parse_tls_client_hello(build_ff148_ech(1));
    ASSERT_TRUE(parsed.is_ok());
    for (const auto &ext : parsed.ok().extensions) {
      first.push_back(ext.type);
    }
  }
  for (uint64 seed = 2; seed < 30; seed++) {
    auto parsed = parse_tls_client_hello(build_ff148_ech(seed));
    ASSERT_TRUE(parsed.is_ok());
    std::vector<uint16> observed;
    for (const auto &ext : parsed.ok().extensions) {
      observed.push_back(ext.type);
    }
    ASSERT_EQ(first, observed);
  }
}

TEST(FirefoxCaptureDifferential, EchIsLastExtension) {
  // Firefox capture: ECH is always the LAST extension in the list.
  for (uint64 seed = 0; seed < 20; seed++) {
    auto parsed = parse_tls_client_hello(build_ff148_ech(seed));
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_FALSE(parsed.ok().extensions.empty());
    ASSERT_EQ(0xFE0Du, parsed.ok().extensions.back().type);
  }
}

TEST(FirefoxCaptureDifferential, EchAbsentWhenDisabled) {
  // When ECH is disabled, ECH extension must not appear.
  for (uint64 seed = 0; seed < 10; seed++) {
    auto parsed = parse_tls_client_hello(build_ff148_no_ech(seed));
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(find_extension(parsed.ok(), 0xFE0Du) == nullptr);
    // But other extensions (delegated_credentials, record_size_limit) must remain.
    ASSERT_TRUE(find_extension(parsed.ok(), 0x0022u) != nullptr);
    ASSERT_TRUE(find_extension(parsed.ok(), 0x001Cu) != nullptr);
  }
}

// ---------------------------------------------------------------------------
// B.3  No GREASE in Firefox (neither extensions nor cipher suites)
// ---------------------------------------------------------------------------

TEST(FirefoxCaptureDifferential, NoGreaseExtensions) {
  for (uint64 seed = 0; seed < 20; seed++) {
    auto parsed = parse_tls_client_hello(build_ff148_ech(seed));
    ASSERT_TRUE(parsed.is_ok());
    for (const auto &ext : parsed.ok().extensions) {
      ASSERT_FALSE(is_grease_value(ext.type));
    }
  }
}

TEST(FirefoxCaptureDifferential, NoGreaseCipherSuites) {
  for (uint64 seed = 0; seed < 20; seed++) {
    for (auto c : extract_cipher_suites(build_ff148_ech(seed))) {
      ASSERT_FALSE(is_grease_value(c));
    }
  }
}

TEST(FirefoxCaptureDifferential, NoGreaseSupportedGroups) {
  for (uint64 seed = 0; seed < 20; seed++) {
    for (auto g : extract_supported_groups(build_ff148_ech(seed))) {
      ASSERT_FALSE(is_grease_value(g));
    }
  }
}

// ---------------------------------------------------------------------------
// B.4  Firefox-specific extensions must be present
// ---------------------------------------------------------------------------

TEST(FirefoxCaptureDifferential, DelegatedCredentialsPresentWithEch) {
  for (uint64 seed = 0; seed < 10; seed++) {
    ASSERT_TRUE(has_extension(build_ff148_ech(seed), 0x0022u));
  }
}

TEST(FirefoxCaptureDifferential, RecordSizeLimitPresentWithEch) {
  for (uint64 seed = 0; seed < 10; seed++) {
    ASSERT_TRUE(has_extension(build_ff148_ech(seed), 0x001Cu));
  }
}

TEST(FirefoxCaptureDifferential, RecordSizeLimitValueMatchesCapture) {
  // Firefox capture: record_size_limit = 0x4001 = 16385 (max TLS 1.3 record).
  // Wire body: 40 01
  for (uint64 seed = 0; seed < 5; seed++) {
    auto body = extract_extension_body(build_ff148_ech(seed), 0x001Cu);
    ASSERT_EQ(2u, body.size());
    ASSERT_EQ(static_cast<uint8>(0x40), static_cast<uint8>(body[0]));
    ASSERT_EQ(static_cast<uint8>(0x01), static_cast<uint8>(body[1]));
  }
}

TEST(FirefoxCaptureDifferential, DelegatedCredentialsBodyMatchesCapture) {
  // Firefox 148 delegated_credentials:
  // 00 22 00 0a 00 08 04 03 05 03 06 03 02 03
  // body = 00 08 04 03 05 03 06 03 02 03
  // [ecdsa_secp256r1_sha256(0403), ecdsa_secp384r1_sha384(0503),
  //  ecdsa_secp521r1_sha512(0603), ecdsa_sha1(0203)]
  static const uint8 kExpected[] = {
      0x00, 0x08,  // sig_hash_algs_length = 8
      0x04, 0x03, 0x05, 0x03, 0x06, 0x03, 0x02, 0x03,
  };
  for (uint64 seed = 0; seed < 5; seed++) {
    auto body = extract_extension_body(build_ff148_ech(seed), 0x0022u);
    ASSERT_EQ(Slice(kExpected, sizeof(kExpected)), body);
  }
}

// ---------------------------------------------------------------------------
// B.5  Supported groups: 7 groups including FFDHE (captures-derived)
// ---------------------------------------------------------------------------

TEST(FirefoxCaptureDifferential, SupportedGroupsExactMatchCapture) {
  // Firefox 148/149: {11EC, 001D, 0017, 0018, 0019, 0100, 0101}
  for (uint64 seed = 0; seed < 10; seed++) {
    auto groups = extract_supported_groups(build_ff148_ech(seed));
    ASSERT_EQ(firefox_linux_desktop_ref_supported_groups, groups);
  }
}

TEST(FirefoxCaptureDifferential, SupportedGroupsCountIsExactly7) {
  for (uint64 seed = 0; seed < 10; seed++) {
    ASSERT_EQ(7u, extract_supported_groups(build_ff148_ech(seed)).size());
  }
}

TEST(FirefoxCaptureDifferential, SupportedGroupsContainAllFfdheGroups) {
  for (uint64 seed = 0; seed < 5; seed++) {
    auto supported_groups = extract_supported_groups(build_ff148_ech(seed));
    std::unordered_set<uint16> gs(supported_groups.begin(), supported_groups.end());
    ASSERT_TRUE(gs.count(0x0100u) != 0);  // ffdhe2048
    ASSERT_TRUE(gs.count(0x0101u) != 0);  // ffdhe3072
  }
}

TEST(FirefoxCaptureDifferential, SupportedGroupsStableAcrossSeeds) {
  // FixedFromFixture: group list must be identical for every seed.
  auto first = extract_supported_groups(build_ff148_ech(1));
  for (uint64 seed = 2; seed < 30; seed++) {
    ASSERT_EQ(first, extract_supported_groups(build_ff148_ech(seed)));
  }
}

// ---------------------------------------------------------------------------
// B.6  No ALPS extension in Firefox (Chrome-exclusive)
// ---------------------------------------------------------------------------

TEST(FirefoxCaptureDifferential, NoAlpsExtension) {
  for (uint64 seed = 0; seed < 10; seed++) {
    ASSERT_FALSE(has_extension(build_ff148_ech(seed), 0x44CDu));
    ASSERT_FALSE(has_extension(build_ff148_ech(seed), 0x4469u));
  }
}

// ---------------------------------------------------------------------------
// B.7  ECH: last position, 0xFE0D, enc_key=32, payload=239 (capture-verified)
// ---------------------------------------------------------------------------

TEST(FirefoxCaptureDifferential, EchPayloadIs239BytesMatchesCapture) {
  // Firefox 148 capture: ECH outer payload = 239 bytes (capture-derived).
  // This is fixed (Firefox does not randomize ECH payload length per-connection).
  for (uint64 seed = 0; seed < 20; seed++) {
    auto parsed = parse_tls_client_hello(build_ff148_ech(seed));
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_EQ(239u, parsed.ok().ech_payload_length);
  }
}

TEST(FirefoxCaptureDifferential, EchEncKeyIs32BytesX25519) {
  // Firefox 148 capture: ECH enc key = 32 bytes (X25519).
  for (uint64 seed = 0; seed < 20; seed++) {
    auto parsed = parse_tls_client_hello(build_ff148_ech(seed));
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_EQ(32u, parsed.ok().ech_declared_enc_length);
    ASSERT_EQ(parsed.ok().ech_declared_enc_length, parsed.ok().ech_actual_enc_length);
  }
}

TEST(FirefoxCaptureDifferential, EchTypeIsFe0dNeverFe02) {
  for (uint64 seed = 0; seed < 10; seed++) {
    ASSERT_TRUE(has_extension(build_ff148_ech(seed), 0xFE0Du));
    ASSERT_FALSE(has_extension(build_ff148_ech(seed), 0xFE02u));
  }
}

// ---------------------------------------------------------------------------
// B.8  Key share: PQ:11EC(1216) + x25519(32) + secp256r1(65)
// ---------------------------------------------------------------------------

TEST(FirefoxCaptureDifferential, KeyShareHasThreeEntries) {
  for (uint64 seed = 0; seed < 10; seed++) {
    auto parsed = parse_tls_client_hello(build_ff148_ech(seed));
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_EQ(3u, parsed.ok().key_share_entries.size());
  }
}

TEST(FirefoxCaptureDifferential, KeyShareNoGreaseEntry) {
  // Firefox: no GREASE key share entry (unlike Chrome).
  for (uint64 seed = 0; seed < 10; seed++) {
    auto parsed = parse_tls_client_hello(build_ff148_ech(seed));
    ASSERT_TRUE(parsed.is_ok());
    for (const auto &e : parsed.ok().key_share_entries) {
      ASSERT_FALSE(is_grease_value(e.group));
    }
  }
}

TEST(FirefoxCaptureDifferential, KeySharePqEntryLength1216) {
  for (uint64 seed = 0; seed < 5; seed++) {
    auto parsed = parse_tls_client_hello(build_ff148_ech(seed));
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

TEST(FirefoxCaptureDifferential, KeyShareX25519EntryLength32) {
  for (uint64 seed = 0; seed < 5; seed++) {
    auto parsed = parse_tls_client_hello(build_ff148_ech(seed));
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

TEST(FirefoxCaptureDifferential, KeyShareSecp256r1EntryLength65) {
  // Firefox 148 capture: secp256r1 key share = 65 bytes (uncompressed X9.62 point).
  for (uint64 seed = 0; seed < 5; seed++) {
    auto parsed = parse_tls_client_hello(build_ff148_ech(seed));
    ASSERT_TRUE(parsed.is_ok());
    std::unordered_map<uint16, uint16> key_lengths;
    for (const auto &e : parsed.ok().key_share_entries) {
      key_lengths[e.group] = e.key_length;
    }
    ASSERT_TRUE(key_lengths.count(0x0017u) != 0);  // secp256r1 present
    ASSERT_EQ(65u, key_lengths[0x0017u]);
  }
}

// ---------------------------------------------------------------------------
// B.9  compress_certificate: zlib + brotli + zstd (Firefox-specific triple)
// ---------------------------------------------------------------------------

TEST(FirefoxCaptureDifferential, CompressCertTripleAlgoMatchesCapture) {
  // Firefox 148 wire: 00 1b 00 07 06 00 01 00 02 00 03
  // body = [list_len=6] [zlib=0001] [brotli=0002] [zstd=0003]
  static const uint8 kExpected[] = {
      0x06,        // algorithm list byte-length = 6
      0x00, 0x01,  // zlib
      0x00, 0x02,  // brotli
      0x00, 0x03,  // zstd
  };
  for (uint64 seed = 0; seed < 5; seed++) {
    auto body = extract_extension_body(build_ff148_ech(seed), 0x001Bu);
    ASSERT_EQ(Slice(kExpected, sizeof(kExpected)), body);
  }
}

TEST(FirefoxCaptureDifferential, CompressCertContainsZlib) {
  // Firefox includes zlib(0x0001); Chrome does not.
  for (uint64 seed = 0; seed < 5; seed++) {
    auto body = extract_extension_body(build_ff148_ech(seed), 0x001Bu);
    ASSERT_FALSE(body.empty());
    const auto *b = reinterpret_cast<const uint8 *>(body.data());
    bool found_zlib = false;
    for (size_t i = 1; i + 1 < body.size(); i += 2) {
      uint16 alg = static_cast<uint16>((static_cast<uint16>(b[i]) << 8) | b[i + 1]);
      if (alg == 0x0001u)
        found_zlib = true;
    }
    ASSERT_TRUE(found_zlib);
  }
}

// ---------------------------------------------------------------------------
// B.10  Firefox-chrome cross-check: PQ group consistent in FF148
// ---------------------------------------------------------------------------

TEST(FirefoxCaptureDifferential, PqGroupConsistentAcrossSupportedGroupsAndKeyShare) {
  for (uint64 seed = 0; seed < 20; seed++) {
    auto parsed = parse_tls_client_hello(build_ff148_ech(seed));
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
// B.11  Firefox vs Chrome structural distinctness
// ---------------------------------------------------------------------------

TEST(FirefoxCaptureDifferential, FirefoxHasMoreSupportedGroupsThanChrome) {
  // Firefox: 7 groups. Chrome: 5. The difference is secp521r1 + FFDHE groups.
  MockRng rng(1);
  auto wire_ff = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                    BrowserProfile::Firefox148, EchMode::Rfc9180Outer, rng);
  rng = MockRng(1);
  auto wire_chrome = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                        BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
  ASSERT_EQ(7u, extract_supported_groups(wire_ff).size());
  ASSERT_EQ(5u, extract_supported_groups(wire_chrome).size());
}

TEST(FirefoxCaptureDifferential, FirefoxHasMoreCipherSuitesThanChrome) {
  // Firefox: 17 suites (no GREASE). Chrome: 16 (1 GREASE + 15).
  MockRng rng(1);
  auto wire_ff = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                    BrowserProfile::Firefox148, EchMode::Rfc9180Outer, rng);
  rng = MockRng(1);
  auto wire_chrome = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                        BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
  ASSERT_EQ(17u, extract_cipher_suites(wire_ff).size());
  ASSERT_EQ(16u, extract_cipher_suites(wire_chrome).size());
}

// ---------------------------------------------------------------------------
// B.12  ALPN body for Firefox: h2, http/1.1
// ---------------------------------------------------------------------------

TEST(FirefoxCaptureDifferential, AlpnBodyMatchesCaptureH2Http11) {
  // Firefox captures: h2 and http/1.1 (same as Chrome).
  static const uint8 kExpected[] = {
      0x00, 0x0c, 0x02, 0x68, 0x32, 0x08, 0x68, 0x74, 0x74, 0x70, 0x2f, 0x31, 0x2e, 0x31,
  };
  for (uint64 seed = 0; seed < 5; seed++) {
    auto body = extract_extension_body(build_ff148_ech(seed), 0x0010u);
    ASSERT_EQ(Slice(kExpected, sizeof(kExpected)), body);
  }
}

// ---------------------------------------------------------------------------
// B.13  Adversarial: Firefox with ECH disabled still keeps Firefox structure
// ---------------------------------------------------------------------------

TEST(FirefoxCaptureDifferential, EchDisabledKeepsAllOtherExtensions) {
  // When ECH disabled, all other Firefox extensions must remain.
  for (uint64 seed = 0; seed < 5; seed++) {
    auto parsed = parse_tls_client_hello(build_ff148_no_ech(seed));
    ASSERT_TRUE(parsed.is_ok());

    // ECH must be gone.
    ASSERT_TRUE(find_extension(parsed.ok(), 0xFE0Du) == nullptr);

    // All other Firefox-specific extensions must still be present.
    ASSERT_TRUE(find_extension(parsed.ok(), 0x0022u) != nullptr);  // delegated_credentials
    ASSERT_TRUE(find_extension(parsed.ok(), 0x001Cu) != nullptr);  // record_size_limit
    ASSERT_TRUE(find_extension(parsed.ok(), 0x001Bu) != nullptr);  // compress_certificate
    ASSERT_TRUE(find_extension(parsed.ok(), 0x000Du) != nullptr);  // signature_algorithms
  }
}

TEST(FirefoxCaptureDifferential, EchDisabledExtensionOrderPreservedExceptEch) {
  // Without ECH, the extension order must be the same as with ECH minus ECH itself.
  for (uint64 seed = 0; seed < 5; seed++) {
    auto parsed = parse_tls_client_hello(build_ff148_no_ech(seed));
    ASSERT_TRUE(parsed.is_ok());

    std::vector<uint16> expected_no_ech;
    for (auto t : firefox_linux_desktop_ref_extension_order) {
      if (t != 0xFE0Du) {
        expected_no_ech.push_back(t);
      }
    }

    std::vector<uint16> observed;
    for (const auto &ext : parsed.ok().extensions) {
      observed.push_back(ext.type);
    }
    ASSERT_EQ(expected_no_ech, observed);
  }
}

// ---------------------------------------------------------------------------
// B.14  Wire size: Firefox hello is larger than Chrome (more key shares + groups)
// ---------------------------------------------------------------------------

TEST(FirefoxCaptureDifferential, FirefoxHelloLargerThanChromeHello) {
  // Firefox has 3 key shares (PQ+x25519+secp256r1) vs Chrome's 3 (GREASE+PQ+x25519),
  // but secp256r1 (65 bytes) > GREASE (1 byte), so Firefox hello is larger.
  MockRng rng(1);
  auto wire_ff = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                    BrowserProfile::Firefox148, EchMode::Rfc9180Outer, rng);
  rng = MockRng(1);
  auto wire_chrome = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678,
                                                        BrowserProfile::Chrome133, EchMode::Rfc9180Outer, rng);
  ASSERT_TRUE(wire_ff.size() > wire_chrome.size());
}

}  // namespace
